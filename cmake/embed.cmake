# Transforme un fichier binaire en source C++ (tableau d'octets) au moment de la compilation.
# cmake -DIN=fichier.bin -DOUT=fichier.cpp -DNAME=symbole -P embed.cmake
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" n)
math(EXPR bytes "${n} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," body "${hex}")
string(REGEX REPLACE "((0x..,){32})" "\\1\n" body "${body}")
file(WRITE "${OUT}" "// Généré par cmake/embed.cmake — ne pas modifier\n#include <cstddef>\nnamespace s3d {\nextern const unsigned char ${NAME}[];\nextern const size_t ${NAME}Size;\nconst unsigned char ${NAME}[] = {\n${body}\n};\nconst size_t ${NAME}Size = ${bytes};\n}\n")
