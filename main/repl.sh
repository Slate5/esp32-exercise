#!/bin/bash

DEPENDENCIES='timeout awk mosquitto_sub mosquitto_pub'
HOST='localhost'
RETAINED_MSG='ESP32-CAM is ready to receive input'
TIMEOUT=5

HISTCONTROL='ignoredups:ignorespace'

for dep in ${DEPENDENCIES}; do
    type -f ${dep} &>/dev/null
    if (( $? != 0 )); then
        printf -- '\033[1;31mDependency \033[3m%s\033[23m not found\033[39m\n' "${dep}"
        exit 1
    fi
done

autocomplete_print_info() {
    printf '\033[1;35m%*s\033[m\n' $(( (COLUMNS + ${#1}) / 2 )) "${1}"
}

autocomplete() {
    local IFS=$'|\n'
    local comps="$(list_commands | awk '/ - / {printf "%s|", $1}')"
    local col_len=0 indent=2
    local line_len=${indent}
    local comp nospace common_substring

    for comp in ${comps[@]}; do
        (( col_len < ${#comp} )) && (( col_len = ${#comp} ))
    done
    (( col_len += 2 ))

    local line_before_cursor="${READLINE_LINE::${READLINE_POINT}}"
    local line_after_cursor="${READLINE_LINE:${READLINE_POINT}}"
    local last_token="${line_before_cursor##*[[:space:]]}"
    local second_last_token="${line_before_cursor%[[:space:]]*}"
    second_last_token="${second_last_token##*[[:space:]]}"

    case "${second_last_token}" in
        flash)
            comps='on|off'
            nospace=yes
            ;;
        intensity)
            autocomplete_print_info 'INFO: provide value between 0 and 255'
            return 0
            ;;
        brightness|contrast|saturation)
            comps='-2|-1|0|1|2'
            nospace=yes
            ;;
        saveas)
            autocomplete_print_info 'INFO: provide name for the file'
            return 0
            ;;
        rotate)
            autocomplete_print_info 'INFO: `rand`, absolute or relative angle'
            return 0
            ;;
    esac

    comps=( $(compgen -W "${comps}" -- "${last_token,,}") )
    if (( $? != 0 )); then
        printf '\a'
        return 1
    fi

    if (( ${#comps[@]} > 1 )); then
        printf -- '%*s' ${indent} ''

        for comp in ${comps[@]}; do
            (( line_len += col_len ))

            if (( COLUMNS < line_len )); then
                (( line_len = col_len + indent ))
                printf -- '\n%*s' ${indent} ''
            fi

            printf -- "\033[3m%-*s\033[23m" ${col_len} "${comp}"
        done

        echo

        common_substring="$(printf "%s\n" "${comps[@]}" |
                            sed -e '$!{N;s/^\(.*\).*\n\1.*$/\1\n\1/;D;}')"
        if [ -n "${common_substring}" ]; then
            READLINE_LINE="${line_before_cursor%${last_token}*}"
            READLINE_LINE+="${common_substring}"
            READLINE_LINE+="${line_after_cursor}"
            READLINE_POINT=$(( ${#line_before_cursor} + ${#common_substring} - ${#last_token} ))
            READLINE_MARK="${READLINE_POINT}"
        fi
    else
        if [[ -z "${nospace}" && ! $(list_commands) =~ "${comps[0]}"( +|\|.* +)- ]]; then
            comps[0]+=' '
        fi

        READLINE_LINE="${line_before_cursor%${last_token}*}"
        READLINE_LINE+="${comps[0]}"
        READLINE_LINE+="${line_after_cursor}"
        READLINE_POINT=$(( ${#line_before_cursor} + ${#comps[0]} - ${#last_token} ))
        READLINE_MARK=${READLINE_POINT}
    fi
}

esp32_output() {
    local topic_out='ESP32/shape_detector/output'

    timeout --foreground ${TIMEOUT} mosquitto_sub ${@} -h "${HOST}" -t "${topic_out}" 2>&1
    if (( $? != 0 )); then
        printf '\033[31mResource temporarily unavailable\033[39m\n'
    fi
}

esp32_input() {
    local topic_in='ESP32/shape_detector/input'

    mosquitto_pub -h "${HOST}" -t "${topic_in}" -m "${1}"
}

get_ack() {
    local ACK ENQ pid
    ENQ=$'\005'
    ACK=$'\006'

    if [[ "$(esp32_output -C 1 -R)" =~ "${ACK}"|"${RETAINED_MSG}" ]]; then
        esp32_output -C 1 --retained-only
    else
        printf '\033[31mNot available. Is ESP32 on and connected?\033[39m\n'
        exit 92
    fi &

    pid=$!

    esp32_input "${ENQ}"

    wait ${pid}
    if (( $? != 0 )); then
        exit 92
    fi
}

list_commands() {
	cat <<-'EOF'
		NOTE: Multiple commands in a line are allowed.
		Example: flash on shoot saveas test.bmp

		ping                - ping ESP32
		shoot               - take a new picture to be used as a reference
		flash <on|off>      - turn on/off the flash LED when taking pictures
		intensity <0-255>   - set flash LED intensity
		brightness <value>  - set image brightness, value between -2 and 2
		contrast <value>    - set image contrast, value between -2 and 2
		saturation <value>  - set image saturation, value between -2 and 2
		save                - save the "shot" picture locally over FTP
		saveas <NAME>       - save the "shot" picture locally as <NAME>
		rotate [angle|rand] - rotate servo by absolute or relative (increment and
		                        decrement) angle, or `rand` for random rotation
		fetch               - try to find an appropriate angle based on the
		                        "shot" picture
		reboot              - reboot ESP32
		help|?              - show this utterly useful text
		quit|exit           - guess what
	EOF
}

trap 'exit 130' SIGINT

set -f -o emacs
bind -x '"\C-i":"autocomplete"'

get_ack

while IFS=' ' read -erp $'\001\e[1;33m\002$\001\e[m\002 ' -a line_arr; do
    history -s "${line_arr[@]}"

    line_arr=( ${line_arr[@],,} )

    for ((i = 0; i < ${#line_arr[@]}; ++i)); do
        TIMEOUT=5

        if [[ $(list_commands) =~ "${line_arr[i]}"(\|[^ ]+)?[[:space:]]+- ]]; then
            command="${line_arr[i]}"
        else
            command="${line_arr[i]} ${line_arr[++i]}"
        fi

        case "${command}" in
            ping)
                get_ack
                continue
                ;;
            rotate\ *rand)
                TIMEOUT=10
                ;;
            save|saveas\ *|fetch|reboot)
                TIMEOUT=120
                ;;
            help|\?)
                list_commands
                continue
                ;;
            quit|exit)
                exit 0
                ;;
            '')
                history -d -1
                continue
                ;;
        esac

        esp32_input "${command}"
        esp32_output -C 1 -R
    done
done

