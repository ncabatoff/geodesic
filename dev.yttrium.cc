#!/usr/bin/env bash
# Geodesic Wrapper Script

set -o pipefail

# Geodesic Settings
export GEODESIC_PORT=${GEODESIC_PORT:-$((30000 + $$%30000))}

OS=$(uname -s)

export USER_ID=$(id -u)
export GROUP_ID=$(id -g)

export options=()
export targets=()

function require_installed() {
  if ! which $1 > /dev/null; then
    echo "Cannot find $1 installed on this system. Please install and try again."
    exit 1
  fi
}

function options_to_env() {
  local kv
  local k
  local v

  for option in ${options[@]}; do
    kv=(${option/=/ })
    k=${kv[0]}                                  # Take first element as key
    k=${k#--}                                   # Strip leading --
    k=${k//-/_}                                 # Convert dashes to underscores
    k=$(echo $k | tr '[:lower:]' '[:upper:]')   # Convert to uppercase (bash3 compat)

    v=${kv[1]}    # Treat second element as value
    v=${v:-true}  # Set it to true for boolean flags

    export $k="$v"
  done
}

function debug() {
  if [ "${VERBOSE}" == "true" ]; then
    echo "[DEBUG] $*"
  fi
}

function use() {
  DOCKER_ARGS=()
  if [ -t 1 ]; then
    # Running in terminal 
    DOCKER_ARGS+=(-it --rm --name="${DOCKER_NAME}" --env LS_COLORS --env TERM --env TERM_COLOR --env TERM_PROGRAM)
    
    if [ -n "$SSH_AUTH_SOCK" ]; then
      if [ `uname -s` == 'Linux' ]; then
        # Bind-mount SSH agent socket into container (linux only)
        DOCKER_ARGS+=( --volume "$SSH_AUTH_SOCK:$SSH_AUTH_SOCK" 
                       --env SSH_AUTH_SOCK
                       --env SSH_CLIENT
                       --env SSH_CONNECTION
                       --env SSH_TTY 
                       --env USER
                       --env USER_ID
                       --env GROUP_ID)
      fi
    fi
  else
    DOCKER_ARGS=()
  fi

  if [ -n "${ENV_FILE}" ]; then
    DOCKER_ARGS+=(--env-file ${ENV_FILE})
  fi

  # allow users to override value of GEODESIC_DEFAULT_ENV_FILE
  local geodesic_default_env_file=${GEODESIC_DEFAULT_ENV_FILE:-~/.geodesic/env}
  if [ -f "${geodesic_default_env_file}" ]; then
      DOCKER_ARGS+=(--env-file=${geodesic_default_env_file})
  fi

  if [ "${OS}" == "Darwin" ]; then
    # Run in privleged mode to enable time synchronization of system clock with hardware clock
    # Implement DNS fix related to https://github.com/docker/docker/issues/24344
    DOCKER_ARGS+=("--dns=${DOCKER_DNS}")
  fi

  if [ -n "${LOCAL_HOME}" ]; then
    local_home=${LOCAL_HOME}
  elif [[ $(uname -r) =~ Microsoft$ ]]; then
    windows_user_name=$(/mnt/c/Windows/System32/cmd.exe /c 'echo %USERNAME%' | tr -d '\r')
    user_local_app_data=$(cmd.exe /c echo %LOCALAPPDATA% | tr -d '\r' | sed -e 's/\\/\//g')

    if [ -d "/mnt/c/Users/${windows_user_name}/AppData/Local/lxss/" ]; then 
      local_home=${user_local_app_data}/lxss${HOME}
    else 
      shopt -s nullglob
      for dir in /mnt/c/Users/${windows_user_name}/AppData/Local/Packages/CanonicalGroupLimited.Ubuntu* ; do
        folder_name=$(basename ${dir})
        local_home=${user_local_app_data}/Packages/${folder_name}/LocalState/rootfs${HOME}
        break
      done
    fi

    if [ -z "${local_home}" ]; then
      echo "ERROR: can't identify user home directory, you may specify path via LOCAL_HOME variable"
      exit 1;
    else
      echo "Detected Windows Subsystem for Linux, mounting $local_home instead of $HOME"
    fi

  else
    local_home=${HOME}
  fi

  if [ "${local_home}" == "/localhost" ]; then
    echo "WARNING: not mounting ${local_home} because it conflicts with geodesic"
  else
    echo "# Mounting ${local_home} into container"
    DOCKER_ARGS+=(--volume=${local_home}:/localhost)
  fi

  DOCKER_ARGS+=(--privileged
                --publish ${GEODESIC_PORT}:${GEODESIC_PORT}
                --name "${DOCKER_NAME}"
                --rm 
                --env KUBERNETES_API_PORT=${GEODESIC_PORT})
  set -o pipefail
  docker ps | grep -q ${DOCKER_NAME} >/dev/null 2>&1
  if [ $? -eq 0 ]; then
    echo "# Attaching to existing ${DOCKER_NAME} session"
    if [ $# -eq 0 ]; then
      set -- "/bin/bash" "-l" "$@"
    fi
    docker exec -it "${DOCKER_NAME}" $*
  else
    echo "# Starting new ${DOCKER_NAME} session from ${DOCKER_IMAGE}"
    echo "# Exposing port ${GEODESIC_PORT}"
    docker run "${DOCKER_ARGS[@]}" ${DOCKER_IMAGE} -l $*
  fi
}

function parse_args() {
  while [[ $1 ]]
  do
    case "$1" in
      -h | --help)
        targets+=("help")
        shift
        ;;
      -v | --verbose)
        export VERBOSE=true
        shift
        ;;
      --*)
        options+=("${1}")
        shift
        ;;
      --) # End of all options
        shift
        ;;
      -*)
        echo "Error: Unknown option: $1" >&2
        exit 1
        ;;
      *=*)
        declare -g "${1}"
        shift
        ;;
      *)
        targets+=("${1}")
        shift
        ;;
    esac
  done
}

function uninstall() {
  echo "# Uninstalling ${DOCKER_NAME}..."
  docker rm -f ${DOCKER_NAME} >/dev/null 2>&1 || true
  docker rmi -f ${DOCKER_IMAGE} >/dev/null 2>&1 || true
  echo "# Not deleting $0"
  exit 0
}

function update(){
  echo "# Installing the latest version of ${DOCKER_IMAGE}"
  docker run --rm ${DOCKER_IMAGE} | bash -s ${DOCKER_TAG}
  if [ $? -eq 0 ]; then
    echo "# ${DOCKER_IMAGE} has been updated."
    exit 0
  else
    echo "Failed to update ${DOCKER_IMAGE}"
    exit 1
  fi
}

function stop() {
   echo "# Stopping ${DOCKER_NAME}..."
   exec docker kill ${DOCKER_NAME} >/dev/null 2>&1
}

function help() {
  echo "Usage: $0 [target] ARGS"
  echo ""
  echo "  Targets:"
  echo "    update     Upgrade geodesic wrapper shell"
  echo "    stop       Stop a running shell"
  echo "    uninstall  Remove geodesic image"
  echo "    <empty>    Enter into a shell"
  echo ""
  echo "  Arguments:"
  echo "    --env-file=... Pass an environment file containing key=value pairs"
  echo ""
}


require_installed tr
require_installed grep

parse_args "$@"
options_to_env

# Docker settings
export DOCKER_IMAGE=cloudposse/dev.yttrium.cc
export DOCKER_TAG=dev
export DOCKER_NAME=${DOCKER_NAME:-`basename $DOCKER_IMAGE`}

if [ -n "${NAME}" ]; then
  export DOCKER_NAME=$(basename "${NAME:-}")
fi

if [ -n "${TAG}" ]; then
  export DOCKER_TAG=${TAG}
fi

if [ -n "${IMAGE}" ]; then
  export DOCKER_IMAGE=${IMAGE:-${DOCKER_IMAGE}:${DOCKER_TAG}}
else
  export DOCKER_IMAGE=${DOCKER_IMAGE}:${DOCKER_TAG}
fi

if [ -n "${PORT}" ]; then
  export GEODESIC_PORT=${PORT}
fi

export DOCKER_DNS=${DNS:-8.8.8.8}

if [ "${GEODESIC_SHELL}" == "true" ]; then
  echo "Cannot run while in a geodesic shell"
  exit 1
fi

if [ -z "${DOCKER_IMAGE}" ]; then
  echo "Error: --image not specified (E.g. --image=cloudposse/foobar.example.com:1.0)"
  exit 1
fi

require_installed docker

docker ps >/dev/null 2>&1
if [ $? -ne 0 ]; then
  echo "Unable to communicate with docker daemon. Make sure your environment is properly configured and then try again."
  exit 1
fi

if [ -z "$targets" ]; then
  # Execute default target
  targets=("use")
fi

for target in $targets; do
  if [ "$target" == "update" ]; then
    update 
  elif [ "$target" == "uninstall" ]; then
    uninstall
  elif [ "$target" == "stop" ]; then
    stop
  elif [ "$target" == "use" ]; then
    use
  elif [ "$target" == "help" ]; then
    help 
  else 
    echo "Unknown target: $target"
    exit 1
  fi
done
