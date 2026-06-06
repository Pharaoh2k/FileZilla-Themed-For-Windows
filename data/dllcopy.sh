#! /bin/sh

set -e

if [ $# -lt 6 ]; then
  echo Wrong number of arguments
  exit 1
fi

suffix="$1"
exepath="$2"
exename="$3"
objdump="$4"
cxx="$5"
searchpath="$6"
ldflags="$7"

searchpath=`echo $searchpath | sed "s/\\(^\\|:\\)\\/c\\/windows[/a-z0-9]*//gi"`

if [ "$ldflags" != "" ]; then
  for flag in $ldflags; do
    if echo $flag | grep '^-L/' > /dev/null 2>&1; then
      searchpath="$searchpath:${flag#-L*}"
    fi
  done
fi

searchpath="$searchpath:`$cxx -print-search-dirs | grep libraries | sed 's/libraries: =//'`"

#echo "Searchpath: $searchpath"

touch "dll_${suffix}_install.nsh"
touch "dll_${suffix}_uninstall.nsh"

process_dll()
{
  if [ ! -f "dlls_$suffix/$1" ] && [ ! -f "dlls_$suffix/${1}.processed" ]; then
    echo "Looking for dependency $1"
    (
      IFS=':'
      for path in $searchpath; do
        if [ -f "$path/.libs/$1" ]; then
          unset IFS
          echo "Found $1"
          cp "$path/.libs/$1" "dlls_$suffix/$1"
          process_file "dlls_$suffix/$1"

          echo "File dlls_$suffix\\$1" >> "dll_${suffix}_install.nsh"
          echo "Delete \$INSTDIR\\$1" >> "dll_${suffix}_uninstall.nsh"
          break
        fi
        if [ -f "$path/$1" ]; then
          unset IFS
          echo "Found $1"
          cp "$path/$1" "dlls_$suffix/$1"
          process_file "dlls_$suffix/$1"

          echo "File dlls_$suffix\\$1" >> "dll_${suffix}_install.nsh"
          echo "Delete \$INSTDIR\\$1" >> "dll_${suffix}_uninstall.nsh"
          break
        fi
      done
      unset IFS
      echo processed > "dlls_$suffix/${1}.processed"
    )
  fi
}

process_dlls()
{
  while [ ! -z "$1" ]; do
    process_dll "$1"
    shift
  done
}

process_file()
{
  process_dlls `$objdump -j .idata -x "$1" | grep 'DLL Name:' | sed 's/ *DLL Name: *//'`
}

if [ -f "$exepath/.libs/$exename" ]; then
  process_file "$exepath/.libs/$exename"
else
  process_file "$exepath/$exename"
fi
