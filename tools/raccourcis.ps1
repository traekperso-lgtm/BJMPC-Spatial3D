# Spatial3D - creation des raccourcis (appele par build-windows.bat)
param(
    [Parameter(Mandatory = $true)][string]$Install
)
$shell = New-Object -ComObject WScript.Shell

function New-Raccourci($dir, $name, $target, $arguments, $desc, $icon) {
    $l = $shell.CreateShortcut((Join-Path $dir "$name.lnk"))
    $l.TargetPath = $target
    $l.Arguments = $arguments
    $l.WorkingDirectory = $Install
    $l.Description = $desc
    $l.IconLocation = $icon
    $l.Save()
}

$bureau  = [Environment]::GetFolderPath('Desktop')
$envoyer = [Environment]::GetFolderPath('SendTo')
$menu    = Join-Path ([Environment]::GetFolderPath('Programs')) 'Spatial3D'
$startup = [Environment]::GetFolderPath('Startup')
New-Item -ItemType Directory -Force $menu | Out-Null

$gui = Join-Path $Install 'Spatial3D.exe'
$cli = Join-Path $Install 'spatial3d-cli.exe'

# Anciens raccourcis (lignes de commande, lecteur de fichiers) : remplaces par l'interface
foreach ($old in @(
        (Join-Path $bureau 'Spatial3D (son 3D).lnk'),
        (Join-Path $bureau 'Spatial3D temps reel.lnk'),
        (Join-Path $menu 'Spatial3D (son 3D).lnk'),
        (Join-Path $menu 'Spatial3D temps reel.lnk'),
        (Join-Path $startup 'Spatial3D temps reel.lnk'),
        (Join-Path $envoyer 'Spatial3D (son 3D).lnk'))) {
    if (Test-Path $old) { Remove-Item $old }
}

New-Raccourci $bureau  'Spatial3D' $gui '' 'Spatialisation 3D' "$gui,0"
New-Raccourci $menu    'Spatial3D' $gui '' 'Spatialisation 3D' "$gui,0"
New-Raccourci $menu    'Spatial3D - retablir le son' $cli '--restaurer' 'Remet la vraie sortie audio par defaut' "$gui,0"
