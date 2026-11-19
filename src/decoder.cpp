#include "decoder.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace s3d {

struct Decoder::Impl {
    AVFormatContext* fmt = nullptr;
    AVCodecContext* cc = nullptr;
    SwrContext* swr = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frm = nullptr;
    int stream = -1;
    int outRate = 0;
    int nch = 0;
    bool ambi = false;
    bool eofInput = false, done = false;
    std::vector<int> ids;
    std::vector<float> fifo;
    size_t fifoPos = 0;
    std::vector<float> tmp;
    std::string infoStr;

    ~Impl() {
        if (swr) swr_free(&swr);
        if (frm) av_frame_free(&frm);
        if (pkt) av_packet_free(&pkt);
        if (cc) avcodec_free_context(&cc);
        if (fmt) avformat_close_input(&fmt);
    }

    void convert(const uint8_t** data, int nb) {
        const int cap = swr_get_out_samples(swr, nb) + 32;
        tmp.resize((size_t)cap * nch);
        uint8_t* out = reinterpret_cast<uint8_t*>(tmp.data());
        const int got = swr_convert(swr, &out, cap, data, nb);
        if (got > 0) fifo.insert(fifo.end(), tmp.begin(), tmp.begin() + (size_t)got * nch);
    }

    void drainDecoder() {
        while (avcodec_receive_frame(cc, frm) == 0) {
            convert(const_cast<const uint8_t**>(frm->extended_data), frm->nb_samples);
            av_frame_unref(frm);
        }
    }

    bool decodeMore() {
        if (done) return false;
        if (eofInput) {
            avcodec_send_packet(cc, nullptr);
            drainDecoder();
            convert(nullptr, 0);   // vide le rééchantillonneur
            done = true;
            return true;
        }
        const int r = av_read_frame(fmt, pkt);
        if (r < 0) { eofInput = true; return true; }
        if (pkt->stream_index == stream) {
            if (avcodec_send_packet(cc, pkt) == AVERROR(EAGAIN)) { drainDecoder(); avcodec_send_packet(cc, pkt); }
            drainDecoder();   // une erreur sur un paquet corrompu n'arrête pas la lecture
        }
        av_packet_unref(pkt);
        return true;
    }
};

Decoder::Decoder() : d_(std::make_unique<Impl>()) {}
Decoder::~Decoder() = default;

int Decoder::channels() const { return d_->nch; }
int Decoder::sampleRate() const { return d_->outRate; }
const std::vector<int>& Decoder::channelIds() const { return d_->ids; }
bool Decoder::ambisonic() const { return d_->ambi; }
std::string Decoder::info() const { return d_->infoStr; }
double Decoder::durationSec() const {
    if (!d_->fmt || d_->fmt->duration == AV_NOPTS_VALUE) return 0;
    return d_->fmt->duration / (double)AV_TIME_BASE;
}

bool Decoder::open(const std::string& path, int outRate, std::string& err) {
    av_log_set_level(AV_LOG_ERROR);
    auto& d = *d_;
    if (avformat_open_input(&d.fmt, path.c_str(), nullptr, nullptr) < 0) { err = "Impossible d'ouvrir le fichier"; return false; }
    if (avformat_find_stream_info(d.fmt, nullptr) < 0) { err = "Flux illisibles"; return false; }
    const AVCodec* codec = nullptr;
    d.stream = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (d.stream < 0 || !codec) { err = "Aucune piste audio decodable"; return false; }
    d.cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(d.cc, d.fmt->streams[d.stream]->codecpar);
    if (avcodec_open2(d.cc, codec, nullptr) < 0) { err = "Decodeur indisponible pour ce format"; return false; }

    d.nch = d.cc->ch_layout.nb_channels;
    if (d.nch <= 0) { err = "Nombre de canaux inconnu"; return false; }

    // Disposition des canaux telle que déclarée par le fichier
    AVChannelLayout lay{};
    if (d.cc->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) av_channel_layout_default(&lay, d.nch);
    else av_channel_layout_copy(&lay, &d.cc->ch_layout);
    d.ambi = lay.order == AV_CHANNEL_ORDER_AMBISONIC;
    d.ids.clear();
    for (int i = 0; i < d.nch; ++i) {
        const AVChannel c = av_channel_layout_channel_from_index(&lay, i);
        d.ids.push_back((c >= 0 && c < 64) ? (int)c : -1);
    }

    d.outRate = outRate > 0 ? outRate : d.cc->sample_rate;
    if (swr_alloc_set_opts2(&d.swr, &d.cc->ch_layout, AV_SAMPLE_FMT_FLT, d.outRate,
                            &d.cc->ch_layout, d.cc->sample_fmt, d.cc->sample_rate, 0, nullptr) < 0 ||
        swr_init(d.swr) < 0) {
        err = "Initialisation du reechantillonneur impossible"; return false;
    }
    d.pkt = av_packet_alloc();
    d.frm = av_frame_alloc();

    char desc[128] = "?";
    av_channel_layout_describe(&lay, desc, sizeof desc);
    char buf[512];
    snprintf(buf, sizeof buf, "%s, %d canaux (%s), %d Hz%s%s",
             codec->long_name ? codec->long_name : codec->name, d.nch, desc, d.cc->sample_rate,
             d.cc->sample_rate != d.outRate ? (" -> " + std::to_string(d.outRate) + " Hz").c_str() : "",
             d.ambi ? ", Ambisonique" : "");
    d.infoStr = buf;
    av_channel_layout_uninit(&lay);
    return true;
}

bool Decoder::seek(double seconds) {
    auto& d = *d_;
    if (!d.fmt) return false;
    const int64_t ts = (int64_t)(std::max(0.0, seconds) * AV_TIME_BASE);
    if (av_seek_frame(d.fmt, -1, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(d.cc);
    swr_close(d.swr);
    swr_init(d.swr);
    d.fifo.clear(); d.fifoPos = 0;
    d.eofInput = false; d.done = false;
    return true;
}

size_t Decoder::read(float* dst, size_t frames) {
    auto& d = *d_;
    const size_t need = frames * d.nch;
    while (d.fifo.size() - d.fifoPos < need && d.decodeMore()) {}
    const size_t avail = std::min(need, d.fifo.size() - d.fifoPos);
    const size_t got = avail / d.nch;
    std::memcpy(dst, d.fifo.data() + d.fifoPos, got * d.nch * sizeof(float));
    d.fifoPos += got * d.nch;
    if (d.fifoPos > (1u << 20)) { d.fifo.erase(d.fifo.begin(), d.fifo.begin() + d.fifoPos); d.fifoPos = 0; }
    return got;
}

} // namespace s3d
