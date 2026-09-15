#!/usr/bin/env bash
set -euo pipefail

bundle_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$bundle_dir"
sha256sum -c MANIFEST.txt

if [[ $# -lt 1 || $# -gt 2 ]]; then
    echo "usage: $0 <cs2-dedicated-server-root> [build-id]" >&2
    exit 64
fi

server_root="$(realpath "$1")"
server_module="$server_root/game/csgo/bin/linuxsteamrt64/libserver.so"
if [[ ! -f "$server_module" ]]; then
    echo "server module not found: $server_module" >&2
    exit 66
fi

build_id="${2:-}"
if [[ -z "$build_id" ]]; then
    for manifest in \
        "$server_root/steamapps/appmanifest_730.acf" \
        "$(dirname "$server_root")/steamapps/appmanifest_730.acf"; do
        if [[ -f "$manifest" ]]; then
            build_id="$(sed -nE 's/^[[:space:]]*"buildid"[[:space:]]*"([0-9]+)".*/\1/p' "$manifest" | head -n 1)"
            [[ -n "$build_id" ]] && break
        fi
    done
fi
if [[ ! "$build_id" =~ ^[0-9]+$ ]]; then
    echo "could not determine app 730 build ID; pass it as the second argument" >&2
    exit 65
fi

expected_build="$(tr -d '\r\n' < EXPECTED_BUILD.txt)"
if [[ "$build_id" != "$expected_build" ]]; then
    echo "build mismatch: bundle=$expected_build server=$build_id" >&2
    exit 65
fi

timestamp="$(date -u +%Y%m%d-%H%M%S)"
work_dir="$(mktemp -d)"
trap 'rm -rf -- "$work_dir"' EXIT
request="$work_dir/request.tsv"
candidate="$work_dir/candidate.tsv"
capture_log="$work_dir/capture.log"
evidence_name="keels2-cs2-current-linux-profile-capture-${timestamp}-evidence"
evidence_dir="$work_dir/$evidence_name"
mkdir -p "$evidence_dir"

printf '%s\n' \
    $'keels2-compatibility-request\t1' \
    $'game\tcs2' \
    "version"$'\t'"$build_id" \
    $'platform\tlinuxsteamrt64' \
    "module"$'\t'"server"$'\t'"$server_module" \
    $'interface\tgame_clients\tSource2GameClients001\tserver' \
    $'interface\tgame_event_manager\tCGameEventManager\tserver' \
    $'interface\tserver\tSource2Server001\tserver' \
    $'interface\tserver_config\tSource2ServerConfig001\tserver' \
    $'slot\tgame_clients\tClientActive\t14' \
    $'slot\tgame_clients\tClientCommand\t17' \
    $'slot\tgame_clients\tClientConnect\t12' \
    $'slot\tgame_clients\tClientDisconnecting\t16' \
    $'slot\tgame_clients\tClientFullyConnected\t15' \
    $'slot\tgame_clients\tClientPutInServer\t13' \
    $'slot\tgame_clients\tClientSettingsChanged\t19' \
    $'slot\tgame_clients\tGameFrame\t19' \
    $'slot\tgame_clients\tOnClientConnected\t11' \
    $'slot\tgame_clients\tValidation\t0' \
    $'slot\tgame_event_manager\tAddListener\t4' \
    $'slot\tgame_event_manager\tLoadEventsFromFile\t2' \
    $'slot\tserver\tInit\t3' \
    $'slot\tserver_config\tConnect\t0' \
    $'slot\tserver_config\tDisconnect\t1' \
    $'pattern\tcs2.base_entity.take_damage\tserver\t55 66 0F EF C0 48 89 E5 41 57 41 56 41 55 49 89 FD 31 FF\t0' \
    > "$request"

set +e
./keels2_compatibility_review capture "$request" "$candidate" 2>&1 | tee "$capture_log"
capture_status=${PIPESTATUS[0]}
set -e

cp "$request" "$evidence_dir/request.tsv"
cp "$capture_log" "$evidence_dir/capture.log"
[[ -f "$candidate" ]] && cp "$candidate" "$evidence_dir/candidate.tsv"
cp MANIFEST.txt EXPECTED_BUILD.txt "$evidence_dir/"
printf 'command=%q %q\nserver_module=%s\nbuild_id=%s\nutc=%s\nuname=%s\n' \
    "$0" "$server_root" "$server_module" "$build_id" "$timestamp" "$(uname -a)" \
    > "$evidence_dir/RUN.txt"
sha256sum "$server_module" > "$evidence_dir/server.sha256"
(cd "$work_dir" && tar -czf "$bundle_dir/$evidence_name.tar.gz" "$evidence_name")
archive="$bundle_dir/$evidence_name.tar.gz"
digest="$(sha256sum "$archive" | awk '{print $1}')"

if [[ -f "$candidate" ]]; then
    size="$(awk -F '\t' '$1=="module" && $2=="server" {print $4}' "$candidate")"
    fnv="$(awk -F '\t' '$1=="module" && $2=="server" {print $5}' "$candidate")"
    offset="$(awk -F '\t' '$1=="pattern" && $2=="cs2.base_entity.take_damage" {print $7}' "$candidate")"
    count="$(awk -F '\t' '$1=="pattern" && $2=="cs2.base_entity.take_damage" {print $6}' "$candidate")"
    printf 'Compatibility candidate captured: cs2-%s-linuxsteamrt64-%s-%s\n' "$build_id" "$size" "$fnv"
    printf 'TakeDamage signature matches: %s at 0x%x\n' "$count" "$offset"
fi
printf 'Evidence archive: %s\nSHA-256: %s\n' "$archive" "$digest"
exit "$capture_status"
