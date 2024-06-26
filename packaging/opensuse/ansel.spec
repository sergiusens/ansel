#
# spec file for package ansel
#
# Copyright (c) 2012 SUSE LINUX Products GmbH, Nuernberg, Germany.
#
# All modifications and additions to the file contributed by third parties
# remain the property of their copyright owners, unless otherwise agreed
# upon. The license for this file, and modifications and additions to the
# file, is the same license as for the pristine package itself (unless the
# license for the pristine package is not an Open Source License, in which
# case the license is the MIT License). An "Open Source License" is a
# license that conforms to the Open Source Definition (Version 1.9)
# published by the Open Source Initiative.

# Please submit bugfixes or comments via http://bugs.opensuse.org/
#


Name:           ansel
Version:        1.0.3
Release:        0
Url:            http://ansel.sourceforge.net
Source0:        %{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-build
BuildRequires:  Mesa-devel
BuildRequires:  OpenEXR-devel
BuildRequires:  cmake
BuildRequires:  dbus-1-glib-devel
BuildRequires:  fdupes
BuildRequires:  gcc-c++
BuildRequires:  gnome-keyring-devel
BuildRequires:  gtk2-devel
BuildRequires:  intltool
BuildRequires:  lensfun-devel
BuildRequires:  libcurl-devel
BuildRequires:  libexiv2-devel
BuildRequires:  libglade2-devel
BuildRequires:  libjpeg-devel
BuildRequires:  liblcms2-devel
BuildRequires:  librsvg-devel
BuildRequires:  libtiff-devel
BuildRequires:  pkg-config
BuildRequires:  sqlite3-devel
BuildRequires:  update-desktop-files

Summary:        A virtual Lighttable and Darkroom
License:        GPL-3.0+
Group:          Productivity/Graphics/Viewers

%description
ansel is a virtual lighttable and darkroom for photographers: it manages
your digital negatives in a database and lets you view them through a zoomable
lighttable. it also enables you to develop raw images and enhance them.


%prep
%setup -q

%build
[ ! -d "build" ] && mkdir  build
cd build

export CXXFLAGS="%{optflags} -fno-strict-aliasing "
export CFLAGS="$CXXFLAGS"

cmake \
        -DCMAKE_INSTALL_PREFIX=%{_prefix} -DCMAKE_INSTALL_LIBDIR=%{_lib} \
        -DCMAKE_BUILD_TYPE=Release \
        -DBINARY_PACKAGE_BUILD=1 ..
%__make %{_smp_mflags} VERBOSE=1

%install
cd build
%make_install
cd ..
%suse_update_desktop_file ansel
find %{buildroot}%{_libdir} -name "*.la" -delete
%find_lang ansel

%__mkdir_p %{buildroot}%{_defaultdocdir}
%__mv %{buildroot}%{_datadir}/doc/ansel %{buildroot}%{_defaultdocdir}

%fdupes %{buildroot}

%files -f ansel.lang
%defattr(-,root,root)
%doc doc/AUTHORS doc/TODO doc/LICENSE
%{_bindir}/ansel
%{_bindir}/ansel-cltest
%{_libdir}/ansel
%{_datadir}/applications/photos.ansel.app.desktop
%{_datadir}/ansel
%{_datadir}/icons/hicolor/*/apps/ansel.*
%{_mandir}/man1/ansel.1.*

%changelog
