Name: liblivebox-edje
Summary: EDJE Script loader for the data provider master
Version: 0.1.16
Release: 1
Group: main/app
License: Flora License
Source0: %{name}-%{version}.tar.gz
BuildRequires: cmake, gettext-tools
BuildRequires: pkgconfig(dlog)
BuildRequires: pkgconfig(eina)
BuildRequires: pkgconfig(evas)
BuildRequires: pkgconfig(edje)
BuildRequires: pkgconfig(eet)
BuildRequires: pkgconfig(vconf)
BuildRequires: pkgconfig(ecore-x)
BuildRequires: pkgconfig(ecore)

%description
EDJE Script loader plugin for the data provider master

%prep
%setup -q

%build
cmake . -DCMAKE_INSTALL_PREFIX=%{_prefix}
make %{?jobs:-j%jobs}

%install
rm -rf %{buildroot}
%make_install
mkdir -p %{buildroot}/usr/share/license

%post

%files -n liblivebox-edje
%manifest liblivebox-edje.manifest
%defattr(-,root,root,-)
/opt/usr/live/script_port/*.so*
/usr/share/license/*
