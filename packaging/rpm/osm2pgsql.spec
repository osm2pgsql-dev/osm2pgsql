###############################################################################

Summary:              OpenStreetMap data to PostgreSQL converter
Name:                 osm2pgsql
Version:              0.90.2
Release:              0%{?dist}
License:              GPL
Group:                Development/Tools
URL:                  http://wiki.openstreetmap.org/wiki/Osm2pgsql

Source:               https://github.com/openstreetmap/%{name}/archive/%{version}.tar.gz

BuildRoot:            %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:        cmake gcc-c++ boost-devel expat-devel zlib-devel bzip2-devel
BuildRequires:        geos-devel postgresql-devel proj-devel proj-epsg lua-devel

###############################################################################

%description
osm2pgsql is a tool for loading OpenStreetMap data into a PostgreSQL / PostGIS 
database suitable for applications like rendering into a map, geocoding with 
Nominatim, or general analysis.

###############################################################################

%prep
%setup -q

%build
mkdir build

pushd build
  cmake -DCMAKE_INSTALL_PREFIX:PATH=%{_prefix} \
      -DCMAKE_INSTALL_LIBDIR:PATH=%{_libdir} \
      -DINCLUDE_INSTALL_DIR:PATH=%{_includedir} \
      -DLIB_INSTALL_DIR:PATH=%{_libdir} \
      -DSYSCONF_INSTALL_DIR:PATH=%{_sysconfdir} \
      -DSHARE_INSTALL_PREFIX:PATH=%{_datadir} \
      -DBUILD_SHARED_LIBS:BOOL=ON ..
  %{__make} %{?_smp_mflags}
popd

%install
%{__rm} -rf %{buildroot}

pushd build
  %{make_install}
popd

%clean
%{__rm} -rf %{buildroot}

###############################################################################

%files
%defattr(-,root,root,0755)
%doc COPYING README
%{_bindir}/%{name}
%{_mandir}/man1/%{name}.1.*
%{_datadir}/%{name}/*.sql
%{_datadir}/%{name}/*.style

###############################################################################

%changelog
* Wed Jun 21 2017 OpenStreetMap Team <dev@openstreetmap.org> - 0.90.2-0
- Initial build. 

