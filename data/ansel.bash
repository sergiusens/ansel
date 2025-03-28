_dt_get_config_dir()
{
  local cur prev words
  _get_comp_words_by_ref -n ":" cur prev words

  local configdir="${HOME}/.config/ansel"
  for (( i=1; i < ${#words[@]}; i=$(( ++i )) )); do
    local candidate="${words[i+1]}"
    __expand_tilde_by_ref candidate
    if [[ "${words[i]}" == "--configdir" && -n "${words[i+1]}" && -d "${candidate}" ]]; then
      configdir="${candidate}"
    fi
  done

  local "$1" && _upvar $1 "${configdir}"
} # _dt_get_config_dir

_dt_get_config_file()
{
  local cur prev words
  _get_comp_words_by_ref -n ":" cur prev words

  local configfile="${HOME}/.config/ansel/anselrc"
  for (( i=1; i < ${#words[@]}; i=$(( ++i )) )); do
    local candidate="${words[i+1]}/anselrc"
    __expand_tilde_by_ref candidate
    if [[ "${words[i]}" == "--configdir" && -n "${words[i+1]}" && -f "${candidate}" ]]; then
      configfile="${candidate}"
    fi
  done

  local "$1" && _upvar $1 "${configfile}"
} # _dt_get_config_file

_ansel()
{
  local cur prev words cword opts dopts
  _get_comp_words_by_ref -n ":" cur prev words cword

  COMPREPLY=()

  # the possible options
  opts="--cachedir --conf --configdir -d --datadir --disable-opencl -h --help --library --localedir --luacmd --moduledir --noiseprofiles -t --tmpdir --version"
  # the possible debug print flags
  dopts="all cache camctl camsupport control dev fswatch history imageio input ioporder lighttable lua masks memory nan opencl params perf pipe pwstorage print signal sql undo"

  case "${prev}" in
    --cachedir|--configdir|--datadir|--localedir|--moduledir|--tmpdir)
      # suggest directories
      _filedir -d
      return 0
      ;;
    --conf)
      # suggest the keys that are in anselrc.
      # if --configdir is given then use the settings in that directory
      local configfile
      _dt_get_config_file configfile
      local keys=$(sed "s/=.*/=/" "${configfile}")
      COMPREPLY=($(compgen -W "${keys}" -- "${cur}"))
      [[ $COMPREPLY == *= ]] && compopt -o nospace
      return 0
      ;;
    -d)
      # suggest the possible debug print flags
      COMPREPLY=($(compgen -W "${dopts}" -- "${cur}"))
      return 0
      ;;
    --library)
      # suggest a filename, directory or ":memory:"
      _filedir
      COMPREPLY+=($(compgen -W ":memory:" -- "${cur}"))
      __ltrim_colon_completions "${cur}" # needed for ":memory:"
      return 0
      ;;
    --noiseprofiles)
      # suggest a filename or directory
      _filedir
      return 0
      ;;
    --luacmd|-t)
      # suggest nothing, there should just be /something/
      return 0
      ;;
  esac

  # if the user started with a '-' suggest the possible command line options
  if [[ "$cur" == -* ]]; then
    COMPREPLY=($(compgen -W "${opts}" -- "${cur}"))
  else
    # otherwise suggest a filename or directory
    _filedir
  fi

  return 0
} # _ansel

_ansel_cli()
{
  local cur prev words cword opts dopts
  _get_comp_words_by_ref -n ":" cur prev words cword

  COMPREPLY=()

  # the possible options
  opts="--width --height --bpp --style --style-overwrite --apply-custom-presets --verbose --version --core --help -h"

  # if there was a --core earlier in the argument list then delegate to _ansel()
  for (( i=1; i < ${cword}; i=$(( ++i )) )); do
    if [[ "${words[i]}" == "--core" ]]; then
      _ansel
      return 0
    fi
  done

  case "${prev}" in
    --width|--height|--bpp)
      # suggest nothing, there should just be /something/
      return 0
      ;;
    --apply-custom-presets)
      # suggest the possible boolean values
      COMPREPLY=($(compgen -W "0 1 true false" -- "${cur}"))
      return 0
      ;;
    --style)
      # suggest styles in the current database, honouring --core --configdir
      local configdir
      _dt_get_config_dir configdir
      local database="${configdir}/data.db"
      [[ -f "${database}" ]] || return 0
      [[ $(type -P sqlite3) ]] || return 0
      local IFS=$'\n'
      local names=($(sqlite3 "${database}" "SELECT name FROM styles"))
      local candidates=($(compgen -W "${names[*]}" -- "${cur}"))
      if [ ${#candidates[*]} -ne 0 ]; then
        COMPREPLY=($(printf '%q\n' "${candidates[@]}"))
      fi
      return 0
      ;;
  esac

  # if the user started with a '-' suggest the possible command line options
  if [[ "$cur" == -* ]]; then
    COMPREPLY=($(compgen -W "${opts}" -- ${cur}))
  else
    # otherwise suggest a filename or directory
    _filedir
  fi

  return 0
} # _ansel_cli

_ansel_generate_cache()
{
  local cur prev words cword opts dopts
  _get_comp_words_by_ref -n ":" cur prev words cword

  COMPREPLY=()

  # the possible options
  opts="-h --help --version --min-mip -m --max-mip --min-imgid --max-imgid --core"

  # if there was a --core earlier in the argument list then delegate to _ansel()
  for (( i=1; i < ${cword}; i=$(( ++i )) )); do
    if [[ "${words[i]}" == "--core" ]]; then
      _ansel
      return 0
    fi
  done

  case "${prev}" in
    --min-mip|-m|--max-mip|--min-imgid|--max-imgid)
      # suggest nothing, there should just be /something/
      return 0
      ;;
  esac

  # ansel-generate-cache doesn't take filenames as arguments, so we can always suggest options
  COMPREPLY=($(compgen -W "${opts}" -- ${cur}))

  return 0
} # _ansel_generate_cache

complete -F _ansel ansel
complete -F _ansel_cli ansel-cli
complete -F _ansel ansel-cltest
complete -F _ansel_generate_cache ansel-generate-cache
