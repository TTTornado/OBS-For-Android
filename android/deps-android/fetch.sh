#!/bin/bash
# 下载依赖源码包到 deps-android/src/
# 运行环境：Windows Git Bash（因为 github 只有 Windows 侧代理可达）
#
#   ./fetch.sh                 下载全部批次
#   ./fetch.sh core            下载某个批次
#   ./fetch.sh jansson ffmpeg  下载指定依赖
#
# 每个归档下载后会做 tar 全量列举（完整性）校验，失败立即退出，不留下可疑的源码包。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=versions.sh
source "$HERE/versions.sh"

# 代理是可选的：本机开发时通常设 OBS_PROXY=http://127.0.0.1:7892，
# 而 CI（GitHub 托管 runner）直连即可，留空则不使用代理。
PROXY="${OBS_PROXY:-}"
SRC="$HERE/src"
mkdir -p "$SRC"

die() { echo "[FATAL] $*" >&2; exit 1; }

# 完整性校验：把归档内容**全量列举**一遍。能同时挡住两类事故：
#   * 下载被截断（-C - 断点续传 / 网络抖动）—— tar 读到最后会报 unexpected EOF；
#   * 把 HTML 错误页、代理返回页当成源码包存下来 —— tar 认不出压缩格式。
# 只按扩展名选解压参数：本清单里只会出现 gz / bz2 / xz 三种。
_archive_list_flags() {
  case "$1" in
    *.tar.gz|*.tgz)   echo "-tzf" ;;
    *.tar.bz2|*.tbz2) echo "-tjf" ;;
    *.tar.xz)         echo "-tJf" ;;
    *)                echo "-tf" ;;   # 未知格式：交给 tar 按魔数自动识别
  esac
}

verify_archive() {
  local f=$1
  local flags
  flags=$(_archive_list_flags "$f")
  tar "$flags" "$f" >/dev/null 2>&1 ||
    { rm -f "$f"; die "归档完整性校验失败（tar 全量列举失败，可能是截断包或 HTML 错误页）: $(basename "$f")"; }
}

# 解析参数：批次名 或 依赖名
TARGETS=""
for a in "$@"; do
  if echo " $DEP_BATCHES " | grep -q " $a "; then
    TARGETS="$TARGETS $(batch_deps "$a")"
  else
    TARGETS="$TARGETS $a"
  fi
done
[ -z "${TARGETS// /}" ] && TARGETS=$(dep_names | tr '\n' ' ')

FAIL=""
SKIP=""
for d in $TARGETS; do
  url=$(dep_url "$d")
  if [ -z "$url" ]; then
    echo "[SKIP] $d : 未配置下载地址"
    SKIP="$SKIP $d"; continue
  fi
  ver=$(dep_version "$d")
  fn="$(basename "$url")"
  out="$SRC/${d}-${ver}-${fn}"
  if [ -s "$out" ]; then
    echo "[CACHE] $d ($ver) 已存在"
    verify_archive "$out"
    continue
  fi
  prox=""
  if [ -n "$PROXY" ]; then
    case "$url" in
      *github.com*) prox="-x $PROXY" ;;
    esac
  fi
  echo "[GET ] $d $ver -> $fn"
  # shellcheck disable=SC2086
  if ! curl -fL --retry 3 --retry-delay 2 -C - -sS $prox -o "$out.part" "$url"; then
    echo "[FAIL] $d 下载失败：$url"
    rm -f "$out.part"; FAIL="$FAIL $d"; continue
  fi
  mv "$out.part" "$out"
  verify_archive "$out"
  printf '       %8d bytes\n' "$(stat -c %s "$out")"
done

echo "=============================="
echo "完成。失败:${FAIL:- 无}  跳过:${SKIP:- 无}"
[ -n "$FAIL" ] && exit 1
exit 0
