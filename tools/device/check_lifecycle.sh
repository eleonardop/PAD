#!/bin/bash
# Verificação real no emulador: abrir, tocar, minimizar, voltar, parar, segundo plano sem som,
# girar, sair (botão, voltar, notificação), recursos liberados, telas em várias resoluções.
# Uso (no CI, dentro do emulador): tools/device/check_lifecycle.sh <apk>
set -u
APK="$1"
PKG=com.eleonardop.pad
ACT=$PKG/.app.MainActivity
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=device_results
mkdir -p "$OUT/shots"
FAILS=0
REPORT="$OUT/relatorio_dispositivo.txt"
: > "$REPORT"
log() { echo "$*" | tee -a "$REPORT"; }
ok() { log "OK     $*"; }
bad() { log "FALHA  $*"; FAILS=$((FAILS + 1)); }

adb root >/dev/null 2>&1; sleep 2
adb wait-for-device
adb install -r -g "$APK" >/dev/null || { log "não instalou"; exit 1; }
adb shell settings put system accelerometer_rotation 0
adb shell settings put system user_rotation 0

pid() { adb shell pidof $PKG | tr -d '\r'; }
services() { adb shell dumpsys activity services $PKG | grep -c "PadPlaybackService" ; }
resumed() { adb shell dumpsys activity activities | grep -E "mResumedActivity|topResumedActivity" | grep -c "$PKG"; }
lc() { adb logcat -d -s PadEngine:I | tr -d '\r'; }
count() { lc | grep -c "$1"; }
threads() { local p; p=$(pid); [ -n "$p" ] && adb shell "cat /proc/$p/task/*/comm" 2>/dev/null | tr -d '\r' | sort | uniq -c | tr '\n' ';'; }
cpu() { local p; p=$(pid); [ -n "$p" ] && adb shell top -b -n 2 -d 2 -p "$p" | tr -d '\r' | awk -v p="$p" '$1==p {c=$9} END {print c+0}'; }
tap() {
  adb shell uiautomator dump /sdcard/ui.xml >/dev/null 2>&1
  adb pull /sdcard/ui.xml "$OUT/ui.xml" >/dev/null 2>&1
  local xy; xy=$(python3 "$HERE/find_view.py" "$OUT/ui.xml" "$1") || { bad "elemento \"$1\" não encontrado na tela"; return 1; }
  adb shell input tap $xy
}
shot() { adb exec-out screencap -p > "$OUT/shots/$1.png"; python3 "$HERE/shot_to_log.py" "$OUT/shots/$1.png" "$1"; }
crashes() { adb logcat -d | grep -cE "FATAL EXCEPTION|Fatal signal|ANR in $PKG"; }

adb logcat -c
log "== 1. Abrir"
adb shell am start -W -n $ACT >/dev/null
sleep 15
[ "$(count 'Stream aberto')" -ge 1 ] && ok "saída de áudio aberta ($(lc | grep 'Stream aberto' | tail -1 | sed 's/.*Stream/Stream/'))" || bad "saída de áudio não abriu"
shot "01_aberto_retrato"

log "== 2. Tocar (G)"
tap "Tom G"; sleep 5
[ "$(services)" -ge 1 ] && ok "serviço em primeiro plano ativo enquanto toca" || bad "serviço não iniciou ao tocar"
shot "02_tocando"
log "   CPU do processo tocando: $(cpu)%   threads: $(threads)"

log "== 3. Minimizar tocando (HOME)"
adb shell input keyevent KEYCODE_HOME; sleep 8
[ "$(services)" -ge 1 ] && ok "continua tocando em segundo plano (serviço vivo)" || bad "parou ao minimizar"
[ "$(count 'Saída de áudio fechada')" -eq 0 ] && ok "saída de áudio continua aberta" || bad "saída de áudio fechou indevidamente"

log "== 4. Voltar e PARAR"
adb shell am start -W -n $ACT >/dev/null; sleep 3
tap "PARAR"; sleep 12
[ "$(services)" -eq 0 ] && ok "serviço encerrado depois que o som terminou" || bad "serviço continua depois de parar"
shot "03_parado"

log "== 5. Segundo plano sem som"
adb shell input keyevent KEYCODE_HOME; sleep 16
[ "$(count 'Saída de áudio fechada')" -ge 1 ] && ok "saída de áudio fechada em segundo plano sem som" || bad "saída de áudio continua aberta em segundo plano"
c=$(cpu); log "   CPU em segundo plano sem som: ${c}%   threads: $(threads)"
awk -v c="$c" 'BEGIN{exit !(c < 2.0)}' && ok "CPU em repouso < 2%" || bad "CPU em repouso alta (${c}%)"

log "== 6. Voltar, tocar e girar a tela"
n0=$(count 'Stream aberto')
adb shell am start -W -n $ACT >/dev/null; sleep 6
[ "$(count 'Stream aberto')" -gt "$n0" ] && ok "saída de áudio reaberta ao voltar" || bad "saída não reabriu ao voltar"
tap "Tom D"; sleep 4
n1=$(count 'Stream aberto')
adb shell settings put system user_rotation 1; sleep 4
shot "04_paisagem_tocando"
[ "$(count 'Stream aberto')" -eq "$n1" ] && ok "girar a tela não reinicia o áudio" || bad "o áudio reiniciou ao girar"
[ "$(services)" -ge 1 ] && ok "continua tocando depois de girar" || bad "parou ao girar"
adb shell settings put system user_rotation 0; sleep 3

log "== 7. Sair (botão, tocando: confirmação)"
mem=$(adb shell dumpsys meminfo $PKG | grep -E "TOTAL PSS|TOTAL:" | head -1 | tr -s ' ')
log "   memória antes de sair: $mem"
tap "Sair do aplicativo"; sleep 2
shot "05_confirmar_sair"
tap "Encerrar"; sleep 6
[ "$(services)" -eq 0 ] && ok "serviço encerrado" || bad "serviço continua depois de Sair"
[ "$(resumed)" -eq 0 ] && ok "tela fechada" || bad "tela continua aberta depois de Sair"
[ "$(count 'Saída de áudio fechada')" -ge 2 ] && ok "saída de áudio fechada ao sair" || bad "saída de áudio não fechou ao sair"
sleep 8; t=$(threads); log "   threads depois de sair (+14 s): ${t:-processo encerrado}"
echo "$t" | grep -qiE "AudioTrack|AAudio|pad-engine|pad-sources|oboe" && bad "sobrou thread de áudio/motor" || ok "nenhuma thread de áudio ou do motor sobrou"
c=$(cpu); log "   CPU depois de sair: ${c:-0}%"
mem=$(adb shell dumpsys meminfo $PKG 2>/dev/null | grep -E "TOTAL PSS|TOTAL:" | head -1 | tr -s ' ')
log "   memória depois de sair: ${mem:-processo encerrado}"

log "== 8. Abrir de novo e sair pelo VOLTAR (sem som)"
adb shell am start -W -n $ACT >/dev/null; sleep 12
n2=$(count 'Saída de áudio fechada')
adb shell input keyevent KEYCODE_BACK; sleep 5
[ "$(resumed)" -eq 0 ] && ok "voltar sem som fecha o app" || bad "voltar não fechou"
[ "$(count 'Saída de áudio fechada')" -gt "$n2" ] && ok "saída de áudio fechada ao voltar" || bad "saída de áudio não fechou ao voltar"

log "== 9. Tocar, VOLTAR (minimiza) e Encerrar pela notificação"
adb shell am start -W -n $ACT >/dev/null; sleep 12
tap "Tom E"; sleep 4
adb shell input keyevent KEYCODE_BACK; sleep 3
[ "$(services)" -ge 1 ] && ok "voltar tocando só minimiza (som continua)" || bad "voltar tocando parou o som"
adb shell cmd statusbar expand-notifications; sleep 3
shot "06_notificacao"
tap "Encerrar"; sleep 6
adb shell cmd statusbar collapse
[ "$(services)" -eq 0 ] && ok "Encerrar pela notificação para o serviço" || bad "serviço continua depois de Encerrar"
[ "$(resumed)" -eq 0 ] && ok "tela fechada pela notificação" || bad "tela continua aberta depois de Encerrar"
sleep 10; t=$(threads); log "   threads depois de Encerrar (+16 s): ${t:-processo encerrado}"; echo "$t" | grep -qiE "AudioTrack|AAudio|pad-engine|pad-sources|oboe" && bad "sobrou thread de áudio/motor" || ok "nada do motor sobrou depois de Encerrar"

log "== 10. Telas em várias resoluções"
for cfg in "720x1280 320 compacto" "1080x2400 420 padrao" "1440x3088 560 s23ultra" "1600x2560 320 tablet"; do
  set -- $cfg
  adb shell wm size "$1"; adb shell wm density "$2"
  adb shell am force-stop $PKG; sleep 1
  adb shell am start -W -n $ACT >/dev/null; sleep 12
  shot "07_tela_$3"
  adb shell settings put system user_rotation 1; sleep 4
  shot "08_tela_$3_paisagem"
  adb shell settings put system user_rotation 0; sleep 2
done
adb shell wm size reset; adb shell wm density reset

n=$(crashes)
[ "$n" -eq 0 ] && ok "nenhum travamento (FATAL/ANR) no logcat" || { bad "$n travamento(s) no logcat"; adb logcat -d | grep -A20 -E "FATAL EXCEPTION|Fatal signal|ANR in" | head -80 | tee -a "$REPORT"; }
adb logcat -d -s PadEngine:I > "$OUT/logcat_padengine.txt"
log ""
log "Falhas: $FAILS"
exit $FAILS
