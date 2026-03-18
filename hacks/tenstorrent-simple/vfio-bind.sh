#!/usr/bin/env bash

set -euo pipefail

PCI_VENDOR_ID="0x1e52"
BIND_DRIVER=""
SHOW_ONLY=0

usage() {
    cat <<'EOF'
Usage:
  vfio-bind.sh --show
  vfio-bind.sh --bind <driver>

Options:
  --show           Print the current driver for all PCI devices with vendor 1e52
  --bind <driver>  Bind all PCI devices with vendor 1e52 to the named driver
EOF
}

list_devices() {
    local sysfs_dev

    for sysfs_dev in /sys/bus/pci/devices/*; do
        [[ -r "${sysfs_dev}/vendor" ]] || continue
        [[ "$(<"${sysfs_dev}/vendor")" == "${PCI_VENDOR_ID}" ]] || continue
        basename "${sysfs_dev}"
    done
}

current_driver() {
    local pci_dev="$1"
    local sysfs_dev="/sys/bus/pci/devices/${pci_dev}"

    if [[ -L "${sysfs_dev}/driver" ]]; then
        basename "$(readlink "${sysfs_dev}/driver")"
    else
        echo "(none)"
    fi
}

bind_result_ok() {
    local target_driver="$1"
    local after_driver="$2"

    if [[ "${after_driver}" == "${target_driver}" ]]; then
        return 0
    fi

    # tenstorrent-simple intentionally returns -ENODEV after calling
    # pci_ignore_hotplug(), so success leaves the device unbound.
    if [[ "${target_driver}" == "tenstorrent-simple" && "${after_driver}" == "(none)" ]]; then
        return 0
    fi

    # tenstorrent-restore intentionally returns -ENODEV after restoring
    # hotplug, so success leaves the device unbound.
    if [[ "${target_driver}" == "tenstorrent-restore" && "${after_driver}" == "(none)" ]]; then
        return 0
    fi

    return 1
}

ensure_driver_available() {
    local driver="$1"

    if [[ -d "/sys/bus/pci/drivers/${driver}" ]]; then
        return 0
    fi

    if modprobe "${driver}" 2>/dev/null; then
        return 0
    fi

    echo "Driver ${driver} is not loaded and could not be modprobed." >&2
    echo "If you built it locally, load it first (for example with insmod) and retry." >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bind)
            [[ $# -ge 2 ]] || {
                echo "--bind requires a driver name" >&2
                usage >&2
                exit 1
            }
            BIND_DRIVER="$2"
            shift 2
            ;;
        --show)
            SHOW_ONLY=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

if (( SHOW_ONLY )) && [[ -n "${BIND_DRIVER}" ]]; then
    echo "Use either --show or --bind <driver>, not both." >&2
    exit 1
fi

if (( ! SHOW_ONLY )) && [[ -z "${BIND_DRIVER}" ]]; then
    usage >&2
    exit 1
fi

if [[ "${EUID}" -ne 0 ]]; then
    echo "Please run as root, for example: sudo $0" >&2
    exit 1
fi

mapfile -t PCI_DEVS < <(list_devices | sort)

if [[ "${#PCI_DEVS[@]}" -eq 0 ]]; then
    echo "No PCI devices found for vendor ${PCI_VENDOR_ID}." >&2
    exit 1
fi

if (( SHOW_ONLY )); then
    for pci_dev in "${PCI_DEVS[@]}"; do
        echo "${pci_dev} $(current_driver "${pci_dev}")"
    done
    exit 0
fi

ensure_driver_available "${BIND_DRIVER}"

# Restrict reprobe for each matching device to the requested driver without changing the
# global ID table for every Tenstorrent device on the host.
for pci_dev in "${PCI_DEVS[@]}"; do
    sysfs_dev="/sys/bus/pci/devices/${pci_dev}"
    driver_override="${sysfs_dev}/driver_override"

    echo "${BIND_DRIVER}" > "${driver_override}"

    if [[ -L "${sysfs_dev}/driver" ]]; then
        echo "${pci_dev}" > "${sysfs_dev}/driver/unbind"
    fi

    echo "${pci_dev}" > /sys/bus/pci/drivers_probe

    after_driver="$(current_driver "${pci_dev}")"
    if ! bind_result_ok "${BIND_DRIVER}" "${after_driver}"; then
        echo "${BIND_DRIVER} did not bind ${pci_dev}." >&2
        exit 1
    fi
done

for pci_dev in "${PCI_DEVS[@]}"; do
    echo "${pci_dev} $(current_driver "${pci_dev}")"
done
