git clone https://aur.archlinux.org/slippi-mainline.git /tmp/slippi-mainline
cd /tmp/slippi-mainline
sed -i 's|https://github.com/project-slippi/dolphin.git|https://github.com/HamletDuFromage/dolphin.git|g' PKGBUILD
makepkg
sudo pacman -U slippi-mainline*.pkg.tar