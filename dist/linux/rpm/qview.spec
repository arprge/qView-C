Name:		qview-c
Version:	7.1
Release:	1%{?dist}
Summary:	Practical and minimal image viewer

License:	GPL-3.0-or-later
URL:		https://interversehq.com/qview/
Source0:	%{name}-%{version}.tar.gz

BuildRequires:	cmake >= 3.16
BuildRequires:	gcc-c++
BuildRequires:	qt6-qtbase-devel
BuildRequires:	qt6-qtsvg-devel
BuildRequires:	qt6-qttools-devel

%description
qView is a Qt image viewer designed with minimalism and usability in mind.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake
%cmake_build

%install
%cmake_install

%files
%license LICENSE
%doc README.md
%{_bindir}/qview
%{_datadir}/applications/com.interversehq.qView.desktop
%{_datadir}/icons/hicolor/*/apps/com.interversehq.qView*.png
%{_datadir}/icons/hicolor/*/apps/com.interversehq.qView*.svg
%{_datadir}/metainfo/com.interversehq.qView.appdata.xml

%changelog
