#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
install_prefix=${1:-"$project_dir/build/install"}

cmake -S "$project_dir" -B "$project_dir/build" \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiSDKCore=ON -DCMAKE_INSTALL_PREFIX="$install_prefix"
cmake --build "$project_dir/build" --parallel 4
ctest --test-dir "$project_dir/build" --output-on-failure --no-tests=error \
    --output-junit "$project_dir/build/json-tests.xml"
cmake --install "$project_dir/build"
python3 -B "$project_dir/tests/install_contents.py" "$install_prefix"
cmake -S "$project_dir/tests/consumer" -B "$project_dir/build/consumer" \
    -DiiJson_DIR="$install_prefix/lib/cmake/iiJson" \
    -DCMAKE_DISABLE_FIND_PACKAGE_iiSDKCore=ON \
    -DCMAKE_PREFIX_PATH="$install_prefix" -DCMAKE_BUILD_TYPE=Release
cmake --build "$project_dir/build/consumer" --parallel 4
ctest --test-dir "$project_dir/build/consumer" --output-on-failure --no-tests=error \
    --output-junit "$project_dir/build/consumer/json-tests.xml"
