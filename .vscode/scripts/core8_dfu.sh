#!/usr/bin/env bash

set -Eeuo pipefail

readonly SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly REPO_DIR="$(cd -- "${SCRIPT_DIR}/../.." && pwd)"
readonly FW_DIR="${REPO_DIR}/firmware"
readonly CORE8_DIR="${FW_DIR}/config/boards/core8"
readonly IMAGE="${FW_DIR}/deliver/fome.bin"
readonly ELF="${FW_DIR}/build/fome.elf"
readonly INI="${FW_DIR}/tunerstudio/generated/fome_core8.ini"
readonly EXPECTED_DEVICE_ID="0x0413"

die() {
	printf 'ERRORE: %s\n' "$*" >&2
	exit 1
}

find_programmer() {
	local -a candidates=()

	if [[ -n "${STM32_PROGRAMMER_CLI:-}" ]]; then
		candidates+=("${STM32_PROGRAMMER_CLI}")
	fi

	candidates+=(
		"/mnt/c/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe"
		"${REPO_DIR}/misc/install/STM32_Programmer_CLI/bin/STM32_Programmer_CLI.exe"
	)

	local candidate
	for candidate in "${candidates[@]}"; do
		if [[ -f "${candidate}" ]]; then
			printf '%s\n' "${candidate}"
			return 0
		fi
	done

	die "STM32_Programmer_CLI non trovato. Installa STM32CubeProgrammer o imposta STM32_PROGRAMMER_CLI."
}

clean_core8() {
	make -C "${FW_DIR}" clean \
		PROJECT_BOARD=core8 \
		PROJECT_CPU=ARCH_STM32F4 \
		BOARD_DIR="${CORE8_DIR}"
	make -C "${FW_DIR}/bootloader" clean \
		PROJECT_BOARD=core8 \
		PROJECT_CPU=ARCH_STM32F4 \
		BOARD_DIR="${CORE8_DIR}"

	# I clean delle singole configurazioni non rimuovono PCH appartenenti ad
	# altri target (per esempio unit_tests), ma GCC prova comunque a caricarli.
	if [[ -d "${FW_DIR}/pch/pch.h.gch" ]]; then
		find "${FW_DIR}/pch/pch.h.gch" -maxdepth 1 -type f -print -delete
	fi
}

build_core8() {
	printf '\n== Compilazione FOME Core8 ==\n'

	# Gli output sono condivisi fra le board: non riusare artefatti di un'altra ECU.
	if [[ -s "${ELF}" ]]; then
		local previous_signature
		previous_signature="$(firmware_signature)"
		[[ "${previous_signature}" == *".core8."* ]] || \
			die "Artefatti di un'altra board presenti. Esegui il task Core8: Clean rebuild."
	fi

	# I timestamp non rilevano un header vecchio ripristinato da Git.
	local source_hash generated_hash generator_stamp
	source_hash="$(sha256sum "${FW_DIR}/console/binary/output_channels.txt" | awk '{print $1}')"
	generated_hash=""
	if [[ -f "${FW_DIR}/console/binary/output_channels_generated.h" ]]; then
		generated_hash="$(sha256sum "${FW_DIR}/console/binary/output_channels_generated.h" | awk '{print $1}')"
	fi
	generator_stamp="${FW_DIR}/build/core8_live_data.sha256"
	if [[ ! -f "${generator_stamp}" || "$(cat "${generator_stamp}")" != "${source_hash} ${generated_hash}" ]]; then
		rm -f "${FW_DIR}/build/generated_live_data.stamp" "${FW_DIR}/build/generated_config.stamp"
	fi

	(
		cd "${CORE8_DIR}"
		bash compile_core8.sh
	)

	# Il generatore locale inserisce il percorso assoluto solo nel commento
	# iniziale; normalizzarlo evita una modifica Git priva di contenuto.
	sed -i "s#based on ${FW_DIR}/#based on #g" \
		"${CORE8_DIR}/connectors/generated_ts_name_by_pin.cpp"

	[[ -s "${IMAGE}" ]] || die "Immagine non generata: ${IMAGE}"
	[[ -s "${ELF}" ]] || die "ELF non generato: ${ELF}"
	[[ -s "${INI}" ]] || die "INI non generato: ${INI}"

	printf '\nBuild completata.\n'
	printf 'Firmware: %s\n' "${IMAGE}"
	printf 'SHA-256: '
	sha256sum "${IMAGE}" | awk '{print $1}'
	verify_artifact_signatures
	generated_hash="$(sha256sum "${FW_DIR}/console/binary/output_channels_generated.h" | awk '{print $1}')"
	printf '%s %s\n' "${source_hash}" "${generated_hash}" > "${generator_stamp}"
}

ini_signature() {
	awk -F '"' '/^[[:space:]]*signature[[:space:]]*=/{print $2; exit}' "${INI}"
}

firmware_signature() {
	strings "${ELF}" | awk '/^rusEFI \(FOME\) /{signature=$0} END{print signature}'
}

verify_artifact_signatures() {
	local ini_sig firmware_sig
	ini_sig="$(ini_signature)"
	firmware_sig="$(firmware_signature)"

	[[ -n "${ini_sig}" ]] || die "Firma non trovata in ${INI}."
	[[ -n "${firmware_sig}" ]] || die "Firma non trovata in ${ELF}."
	[[ "${firmware_sig}" == *".core8."* ]] || \
		die "Il firmware generato non è Core8: ${firmware_sig}"
	[[ "${ini_sig}" == "${firmware_sig}" ]] || \
		die "Firma firmware e INI non corrispondono: '${firmware_sig}' != '${ini_sig}'."

	printf 'Firma:    %s\n' "${firmware_sig}"
}

flash_core8() {
	[[ -s "${IMAGE}" ]] || die "${IMAGE} non esiste. Esegui prima il task di compilazione."
	[[ -s "${ELF}" ]] || die "${ELF} non esiste. Esegui prima il task di compilazione."
	[[ -s "${INI}" ]] || die "${INI} non esiste. Esegui prima il task di compilazione."
	verify_artifact_signatures

	local programmer
	programmer="$(find_programmer)"

	printf '\n== Rilevamento ECU DFU ==\n'
	local devices
	devices="$("${programmer}" -l usb)"
	printf '%s\n' "${devices}"

	local count
	count="$(awk '/Device Index/{count++} END{print count+0}' <<<"${devices}")"
	[[ "${count}" -eq 1 ]] || die "Attesa esattamente una ECU DFU, rilevate: ${count}."

	local port serial device_id
	port="$(awk -F: '/Device Index/{gsub(/[[:space:]]/, "", $2); print $2; exit}' <<<"${devices}")"
	serial="$(awk -F: '/Serial number/{gsub(/[[:space:]]/, "", $2); print $2; exit}' <<<"${devices}")"
	device_id="$(awk -F: '/Device ID/{gsub(/[[:space:]]/, "", $2); print tolower($2); exit}' <<<"${devices}")"

	[[ -n "${port}" ]] || die "Porta DFU non rilevata."
	[[ -n "${serial}" ]] || die "Seriale DFU non rilevato."
	[[ "${device_id}" == "${EXPECTED_DEVICE_ID}" ]] || \
		die "Device ID ${device_id:-sconosciuto}; Core8 richiede ${EXPECTED_DEVICE_ID}."

	local image_arg="${IMAGE}"
	if [[ "${programmer}" == *.exe ]]; then
		command -v wslpath >/dev/null 2>&1 || die "wslpath non disponibile."
		image_arg="$(wslpath -w "${IMAGE}")"
	fi

	printf '\n== Flash e verifica Core8 ==\n'
	printf 'ECU:      %s (%s, %s)\n' "${serial}" "${port}" "${device_id}"
	printf 'Immagine: %s\n' "${IMAGE}"

	"${programmer}" -q -c "port=${port}" "sn=${serial}" \
		-w "${image_arg}" 0x08000000 -v

	printf '\n== Avvio firmware ==\n'
	"${programmer}" -q -c "port=${port}" "sn=${serial}" -g 0x08000000

	local remaining
	remaining="$("${programmer}" -l usb)"
	count="$(awk '/Device Index/{count++} END{print count+0}' <<<"${remaining}")"
	[[ "${count}" -eq 0 ]] || die "L'ECU risulta ancora in modalità DFU."

	printf '\nFlash verificato; firmware avviato e ECU uscita dalla modalità DFU.\n'
}

show_ini() {
	[[ -s "${INI}" ]] || die "${INI} non esiste. Esegui prima il task di compilazione."
	printf 'INI TunerStudio:\n%s\n' "${INI}"
	if command -v wslpath >/dev/null 2>&1; then
		wslpath -w "${INI}"
	fi
	verify_artifact_signatures
}

case "${1:-all}" in
	build)
		build_core8
		;;
	rebuild)
		clean_core8
		build_core8
		;;
	flash)
		flash_core8
		;;
	all)
		build_core8
		flash_core8
		;;
	ini)
		show_ini
		;;
	*)
		die "Uso: $0 {build|rebuild|flash|all|ini}"
		;;
esac
