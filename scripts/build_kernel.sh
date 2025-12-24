#!/bin/bash -e

PACKAGE_SRC_NAME="linux-riscv-spacemit-generic"
CONFIG_FILE="k3_defconfig"
CLEAN_CMD="make distclean && make -C tools/perf clean &&
    VERSION=\$(grep -oP '^VERSION\\s*=\\s*\\K\\d+' Makefile)
    PATCHLEVEL=\$(grep -oP '^PATCHLEVEL\\s*=\\s*\\K\\d+' Makefile)
    SUBLEVEL=\$(grep -oP '^SUBLEVEL\\s*=\\s*\\K\\d+' Makefile)
    export KERNELRELEASE=\$VERSION.\$PATCHLEVEL.\$SUBLEVEL &&
    rm -f ../linux-headers-\$KERNELRELEASE_*_riscv64.deb &&
    rm -f ../linux-image-\$KERNELRELEASE_*_riscv64.deb &&
    rm -f ../linux-image-\$KERNELRELEASE-dbg_*_riscv64.deb &&
    rm -f ../linux-tools-\$KERNELRELEASE_*_riscv64.deb &&
    rm -f ../linux-libc-dev_*_riscv64.deb &&
    rm -f ../linux-tools-common_*_all.deb &&
    rm -f ../${PACKAGE_SRC_NAME}_*_riscv64.*
"
BUILD_CMD="make $CONFIG_FILE && make -j\${JOBS:-\$(nproc)}"
BUILD_DEB_CMD="make $CONFIG_FILE &&
    sed -i '/CONFIG_INITRAMFS_SOURCE=/d' arch/riscv/configs/$CONFIG_FILE
    VERSION=\$(grep -oP '^VERSION\\s*=\\s*\\K\\d+' Makefile)
    PATCHLEVEL=\$(grep -oP '^PATCHLEVEL\\s*=\\s*\\K\\d+' Makefile)
    SUBLEVEL=\$(grep -oP '^SUBLEVEL\\s*=\\s*\\K\\d+' Makefile)
    export KERNELRELEASE=\$VERSION.\$PATCHLEVEL.\$SUBLEVEL
    export LOCALVERSION=\"-generic\"
    KDEB_SOURCENAME=$PACKAGE_SRC_NAME \\
    KDEB_PKGVERSION=\$KERNELRELEASE-\$(TZ=Asia/Shanghai date +\"%Y%m%d%H%M%S\") \\
    KDEB_CHANGELOG_DIST=resolute-porting \\
    make -j\${JOBS:-\$(nproc)} bindeb-pkg
"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SOURCE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SOURCE_PARENT_DIR="$(cd "$SOURCE_DIR/.." && pwd)"
DOCKER_REPO="${DOCKER_REPO:-harbor.spacemit.com/bianbu}"
IMAGE_NAME="${IMAGE_NAME:-k3-bsp-builder:latest}"
CROSS_COMPILE="${CROSS_COMPILE:-riscv64-unknown-linux-gnu-}"
TOOLCHAIN_PATH="${TOOLCHAIN_PATH:-/opt/spacemit-toolchain-linux-glibc-x86_64-v1.2.2/bin}"
DIRECT_BUILD="${DIRECT_BUILD:-0}"
JOBS=""

CLEAN=false
BUILD_DEB=false
ACTION="build"
DEBUG=false

# Function to handle symlinks within git directory
handle_git_internal_symlinks() {
    local git_dir="$1"
    local container_git_dir="$2"
    local -n git_mounts_ref="$3"  # Reference to the git mounts array
    [ "$DEBUG" = true ] && echo "Scanning for symlinks in git directory: $git_dir"
    # Find all symlinks in the git directory
    while IFS= read -r -d '' symlink; do
        if [ -L "$symlink" ]; then
            local real_target
            real_target="$(readlink -f "$symlink")"
            local relative_path="${symlink#$git_dir/}"
            local container_target="$container_git_dir/$relative_path"
            if [ -e "$real_target" ]; then
                [ "$DEBUG" = true ] && echo "Found symlink: $relative_path -> $real_target"
                # Store mounts in the array - add both -v and the mount path
                git_mounts_ref+=("-v" "$real_target:$container_target")
            else
                [ "$DEBUG" = true ] && echo "Warning: Broken symlink found: $symlink -> $real_target"
            fi
        fi
    done < <(find "$git_dir" -type l -print0 2>/dev/null)
}

# Function to setup all container volume mounts (including git handling)
setup_volume_mounts() {
    local project_dir="$1"
    local package_dir="$2"
    local git_path="$package_dir/.git"
    local container_base
    container_base="/workspace/$(basename "$package_dir")"
    # Local git_mounts array for this function
    local git_mounts=()
    [ "$DEBUG" = true ] && echo "Setting up container volume mounts..."
    # Initialize with basic mounts
    VOLUME_MOUNTS=("-v" "$project_dir:/workspace" "-w" "$container_base")
    # Handle git directory if exists
    if [ -e "$git_path" ]; then
        local actual_git_dir
        local container_git_path="$container_base/.git"
        if [ -L "$git_path" ]; then
            [ "$DEBUG" = true ] && echo "Detected .git as symbolic link, resolving real git directory..."
            actual_git_dir="$(readlink -f "$git_path")"
            if [ ! -d "$actual_git_dir" ]; then
            echo "Error: Cannot find real git directory: $actual_git_dir"
            echo "Please ensure the git repository is accessible."
                exit 1
            fi
            # Set up main git directory mount
            git_mounts+=("-v" "$actual_git_dir:$container_git_path")
            [ "$DEBUG" = true ] && echo "Will mount real git directory: $actual_git_dir -> $container_git_path"
        elif [ -d "$git_path" ]; then
            [ "$DEBUG" = true ] && echo "Found normal .git directory"
            actual_git_dir="$git_path"
            # For normal directories, no additional mount needed as it's part of the main project mount
        else
            [ "$DEBUG" = true ] && echo "Warning: .git exists but is neither a directory nor a symlink"
            actual_git_dir=""
        fi
        # Handle symlinks within the git directory
        if [ -n "$actual_git_dir" ]; then
            handle_git_internal_symlinks "$actual_git_dir" "$container_git_path" git_mounts
        fi
    else
        [ "$DEBUG" = true ] && echo "Warning: No .git directory found, some git operations may fail"
    fi
    # Add all git mounts (both main directory and internal symlinks)
    if [ ${#git_mounts[@]} -gt 0 ]; then
        VOLUME_MOUNTS+=("${git_mounts[@]}")
        [ "$DEBUG" = true ] && echo "Added $(( ${#git_mounts[@]} / 2 )) git-related mounts"
    fi
    [ "$DEBUG" = true ] && echo "Total volume mounts configured: $(( (${#VOLUME_MOUNTS[@]} + 1) / 2 ))"
    # fix this function will exit if bash set -e
    return 0
}

# Setup container volume mounts (includes git handling)
setup_volume_mounts "$SOURCE_PARENT_DIR" "$SOURCE_DIR"

usage() {
    echo "Container-based build wrapper (universal version)"
    echo "Usage: $(basename "$0") [OPTIONS] [COMMAND]"
    echo ""
    echo "Options:"
    echo "  -c, --clean          Clean build (CLEAN_CMD before BUILD[_DEB]_CMD)"
    echo "  -d, --deb            Build DEB packages (BUILD_DEB_CMD)"
    echo "  -h, --help           Show this help message"
    echo "  -j, --jobs NUM       Number of parallel jobs (default: nproc)"
    echo "  -x, --debug          Enable debug output (show docker command)"
    echo ""
    echo "Commands (run inside container):"
    echo "  build               Build (BUILD_CMD, default)"
    echo "  shell               Interactive shell"
    echo ""
    echo "Examples:"
    echo "  $(basename "$0")                    # Build in container"
    echo "  $(basename "$0") -c -d              # Clean build with DEB packages"
    echo "  $(basename "$0") shell              # Interactive container shell"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean)
            CLEAN=true
            shift
            ;;
        -d|--deb)
            BUILD_DEB=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        -j|--jobs)
            JOBS="$2"
            shift 2
            ;;
        -x|--debug)
            DEBUG=true
            shift
            ;;
        build|shell)
            ACTION="$1"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

if [[ -n "$JOBS" && ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid jobs value: $JOBS (must be a positive integer)"
    exit 1
fi

# Check if Docker is available
if [ -z "$DIRECT_BUILD" ] || [ "$DIRECT_BUILD" = "0" ]; then
    if ! command -v docker &> /dev/null; then
        echo "Error: Docker not found. Please install Docker."
        exit 1
    fi
fi

# Set DEBEMAIL and DEBFULLNAME environment variable
if [ -z "$DEBEMAIL" ]; then
    if [ -n "$MAIL" ]; then
        DEBEMAIL="$MAIL"
    else
        DEBEMAIL="$(id -nu)@$(hostname -f 2>/dev/null || hostname)"
    fi
fi

if [ -z "$DEBFULLNAME" ]; then
    if [ -n "$NAME" ]; then
        DEBFULLNAME="$NAME"
    elif echo "$DEBEMAIL" | grep -qE '^([^<]+)\s*<[^>]+>$'; then
        # If DEBEMAIL matches "name <email>", extract name as DEBFULLNAME
        DEBFULLNAME="$(echo "$DEBEMAIL" | sed -n 's/^\([^<][^<]*\)<.*$/\1/p' | sed 's/[[:space:]]*$//')"
    else
        DEBFULLNAME="$(id -nu)"
    fi
fi

CONTAINER_ENV=("-e" "ARCH=riscv" "-e" "CROSS_COMPILE=$CROSS_COMPILE" "-e" "PATH=$TOOLCHAIN_PATH:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" "-e" "DEBEMAIL=$DEBEMAIL" "-e" "DEBFULLNAME=$DEBFULLNAME")
if [[ -n "$JOBS" ]]; then
    CONTAINER_ENV+=("-e" "JOBS=$JOBS")
fi

# Function to create user permission files for container
create_user_files() {
    local current_user current_group
    current_user="$(whoami):x:$(id -u):$(id -g):$(whoami),,,:/home/$(whoami):/bin/bash"
    current_group="$(id -gn):x:$(id -g):"
    # Create or update .passwd and .group files
    if [ ! -f "$SOURCE_DIR/.passwd" ] || \
       [ ! -f "$SOURCE_DIR/.group" ] || \
       [ "$current_user" != "$(grep "^$(whoami):" "$SOURCE_DIR/.passwd" 2>/dev/null)" ] || \
       [ "$current_group" != "$(grep "^$(id -gn):" "$SOURCE_DIR/.group" 2>/dev/null)" ]; then
        cp /etc/passwd "$SOURCE_DIR/.passwd.tmp"
        cp /etc/group "$SOURCE_DIR/.group.tmp"
        if ! grep -q "^$(whoami):" "$SOURCE_DIR/.passwd.tmp"; then
            echo "$current_user" >> "$SOURCE_DIR/.passwd.tmp"
        fi
        if ! grep -q "^$(id -gn):" "$SOURCE_DIR/.group.tmp"; then
            echo "$current_group" >> "$SOURCE_DIR/.group.tmp"
        fi
        mv "$SOURCE_DIR/.passwd.tmp" "$SOURCE_DIR/.passwd"
        mv "$SOURCE_DIR/.group.tmp" "$SOURCE_DIR/.group"
    fi
}

# Create user permission files
create_user_files

# Function to run command in container
run_in_container() {
    local cmd="$1"
    if [ "$DEBUG" = true ]; then
        echo "--- Docker/Podman full command ---"
        local full_cmd
        full_cmd="docker run --rm -it --init"
        for v in "${VOLUME_MOUNTS[@]}"; do
            full_cmd+=" $v"
        done
        for e in "${CONTAINER_ENV[@]}"; do
            full_cmd+=" $e"
        done
        full_cmd+=" -v $SOURCE_DIR/.passwd:/etc/passwd:ro"
        full_cmd+=" -v $SOURCE_DIR/.group:/etc/group:ro"
        full_cmd+=" -u $(id -u):$(id -g)"
        full_cmd+=" $DOCKER_REPO/$IMAGE_NAME"
        full_cmd+=" bash -c \"$cmd\""
        echo "$full_cmd"
        echo "--- End docker command ---"
    fi
    docker run --rm -it --init \
        "${VOLUME_MOUNTS[@]}" \
        "${CONTAINER_ENV[@]}" \
        -v "$SOURCE_DIR/.passwd:/etc/passwd:ro" \
        -v "$SOURCE_DIR/.group:/etc/group:ro" \
        -u "$(id -u):$(id -g)" \
        "$DOCKER_REPO/$IMAGE_NAME" \
        bash -c "$cmd"
}

# Run command directly or in container based on DIRECT_BUILD
run_command() {
    local cmd="$1"
    if [ -n "$DIRECT_BUILD" ] && [ "$DIRECT_BUILD" != "0" ]; then
        [ "$DEBUG" = true ] &&  echo "[DIRECT_BUILD] Executing locally: $cmd"
        command -v riscv64-unknown-linux-gnu-gcc > /dev/null || \
        (echo "Install cross compile and add to PATH" && exit 1)
        export ARCH=riscv
        export CROSS_COMPILE=riscv64-unknown-linux-gnu-
        if [[ -n "$JOBS" ]]; then
            export JOBS
        fi
        bash -c "$cmd"
    else
        [ "$DEBUG" = true ] && echo "Building in container..."
        run_in_container "$cmd"
    fi
}

# Execute container actions
case "$ACTION" in
    build)
        if [[ "$CLEAN" == true ]]; then
            run_command "$CLEAN_CMD"
        fi
        if [[ "$BUILD_DEB" == true ]]; then
            run_command "$BUILD_DEB_CMD"
        else
            run_command "$BUILD_CMD"
        fi
        echo "Build completed successfully!"
        ;;
    shell)
        run_in_container "bash"
        echo "Container shell session ended."
        ;;
    *)
        echo "Unknown action: $ACTION"
        usage
        exit 1
        ;;
esac
