#! /bin/sh

set -e

bundle="$1"
searchdirs="$2"
executabledir="${3:-${bundle}/Contents/MacOS/}"
relframeworks="${4:-../Frameworks}"

if [ ! -d "$bundle" ]; then
  echo "$bundle is not an application bundle"
  exit 1
fi

frameworks="${bundle}/Contents/Frameworks"

mkdir -p "$frameworks"
rm -f "${frameworks}/"*.processed

absreadlink()
{
  if [ -L "$1" ]; then
    local target=`readlink "$1"`
    if [ "${target:0:1}" != "/" ]; then
      local dir=`dirname "$1"`
      target="$dir/$target"
    fi
    if [ -L "$target" ]; then
      absreadlink "$target"
    else
      echo "$target"
    fi
  else
    echo "$1"
  fi
}

find_dylib()
{
  local path="$1"
  local dylib="$2"

  local name="${dylib##*/}"
  if [ -f "$path/$name" ]; then
    echo "$path/$name"
    exit    
  fi

  local dirs="$LD_LIBRARY_PATH"
  while [ ! -z ${dirs} ]; do
    local dir="${dirs%%:*}"
    dirs="${dirs#*:}"
    if [ -f "$dir/$name" ]; then
      echo "$dir/$name"
      exit    
    fi
  done
  
  echo $dylib
}

process_dylib()
{
  local file="$1"
  local origin="$2"
  local dylib="$3"
  local original_dylib="$dylib"

  case $dylib in
    @rpath/*)
      dylib=`find_dylib "${origin%/*}" "$dylib"`;;
  esac

  case $dylib in
    /usr/*|/System/*)
      exit;;
  esac

  local resolved=`absreadlink "$dylib"`
  local name="${resolved##*/}"

  if [ ! -f "${frameworks}/$name" ] && [ ! -f "${frameworks}/$name.processed" ]; then
    touch "${frameworks}/$name.processed"

    local dirs=$searchdirs
    while [ ! -z "$dirs" ]; do
      local dir=${dirs%%:*}
      dirs=${dirs#*:}
      if [ "$dir" = "$dirs" ]; then
        dirs=
      fi

      if [ -f "$dir/.libs/$name" ]; then
        echo "Found dependency $name"
        cp "$dir/.libs/$name" "${frameworks}/$name"
        break
      elif [ -f "$dir/$name" ]; then
        echo "Found dependency $name"
        cp "$dir/$name" "${frameworks}/$name"
        break
      elif [ -f "$dylib" ]; then
        echo "Found dependency $name"
        cp "$dylib" "${frameworks}/$name"
        break
      fi
    done

    if [ ! -f "${frameworks}/$name" ]; then
      echo "Dependency $name not found"
      exit 1
    fi

    install_name_tool -id "$name" "${frameworks}/$name"

    # dylibs themselves have dependencies. Process them too
    process_file "${frameworks}/$name" "$dylib"
  fi

  if echo "$file" | grep '.\(dylib\|so\)$' >/dev/null 2>&1; then
    install_name_tool -change "${original_dylib}" "@loader_path/$name" "$file"
  else
    install_name_tool -change "${original_dylib}" "@executable_path/${relframeworks}/$name" "$file"
  fi
}

process_dylibs()
{
  local file="$1"
  local origin="$2"
  while [ ! -z "$3" ]; do
    process_dylib "$file" "$origin" "$3"
    shift
  done
}

process_file()
{
  local file="$1"
  local origin="${2:-$1}"

  process_dylibs "$file" "$origin" `otool -L "$file" | grep -v ':$' | grep "dylib\\|\\.so" | sed 's/^[[:blank:]]*//' | sed 's/ .*//' | grep -v '^/usr/\|^/System/'`
  process_dylibs "$file" "$origin" `otool -L "$file" | grep -v ':$' | grep "dylib\\|\\.so" | sed 's/^[[:blank:]]*//' | sed 's/ .*//' | grep '^/usr/local/'`
}

if [ -z "$3" ]; then
  rm -f "${frameworks}/"*.dylib
fi

for file in "${executabledir}"*; do
  process_file "$file"
done

rm -f "${frameworks}/"*.processed

echo Dependencies copied
