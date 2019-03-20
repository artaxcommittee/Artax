
Debian
====================
This directory contains files used to package artaxd/artax-qt
for Debian-based Linux systems. If you compile artaxd/artax-qt yourself, there are some useful files here.

## artax: URI support ##


artax-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install artax-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your artaxqt binary to `/usr/bin`
and the `../../share/pixmaps/artax128.png` to `/usr/share/pixmaps`

artax-qt.protocol (KDE)

