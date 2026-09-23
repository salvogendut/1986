Name:           1986
Version:        0.2.0
Release:        1%{?dist}
Summary:        Commodore C128DCR emulator

License:        GPL-2.0-or-later
URL:            https://github.com/salvogendut/1986
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  pkgconfig(sdl3)
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

%description
1986 is an SDL3-based emulator of the Commodore C128DCR computer. It
supports the native 40-column VIC-IIe and 80-column VDC displays, SID audio,
joystick and mouse input, cartridges, and virtual disk images. Machine ROMs
are not included and must be supplied separately.

%prep
%autosetup

%build
autoreconf -fiv
%configure
%make_build

%install
%make_install

%check
make -C tests check
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.salvogendut.Emulator1986.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/io.github.salvogendut.Emulator1986.metainfo.xml

%files
%license LICENSE
%doc README.md INSTALL.md USAGE.md CONTROLS.md
%{_bindir}/%{name}
%{_mandir}/man1/%{name}.1*
%{_datadir}/applications/io.github.salvogendut.Emulator1986.desktop
%{_datadir}/metainfo/io.github.salvogendut.Emulator1986.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/io.github.salvogendut.Emulator1986.png
%dir %{_datadir}/%{name}
%dir %{_datadir}/%{name}/roms
%{_datadir}/%{name}/roms/README

%changelog
* Wed Sep 23 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.2.0-1
- Add browser support, snapshots, multiprocessor monitor, improved tape controls,
  and full machine power cycling.

* Mon Sep 21 2026 Salvatore Bognanni <salvogendut@gmail.com> - 0.1.0-1
- Add branded icons and cross-platform release packaging.
