#!/bin/bash
set -e
cd "$(dirname "$0")"

OUT=cap_results.txt
> "$OUT"

run_config() {
    local label="$1"
    local c_per="$2"
    local esr_per="$3"
    local esl_per="$4"
    local n="$5"

    sed -e "s|^\.param bulk_c_per = .*|.param bulk_c_per = $c_per  bulk_esr_per = $esr_per  bulk_esl_per = $esl_per  bulk_n = $n|" \
        cap_transient.cir > tmp_sweep.cir

    local out
    out=$(timeout 120 ngspice -b tmp_sweep.cir 2>/dev/null | \
          grep -E '^vbus_(pre|t1us|t2us|t5us|t10us|t30us|t100us|t300us|min)' | \
          awk '{print $3}' | tr '\n' ' ')

    local pre t1 t2 t5 t10 t30 t100 t300 vmin
    read -r pre t1 t2 t5 t10 t30 t100 t300 vmin <<< "$out"

    mv waves.csv "waves_${label}.csv" 2>/dev/null || true

    printf "%-25s pre=%5.2f  t+1us=%5.2f  +2us=%5.2f  +5us=%5.2f  +10us=%5.2f  +30us=%5.2f  +100us=%5.2f  min=%5.2f\n" \
           "$label" "$pre" "$t1" "$t2" "$t5" "$t10" "$t30" "$t100" "$vmin" | tee -a "$OUT"
}

echo "Bus cap transient — 10A→30A step at t=200us, rise=100ns"   | tee -a "$OUT"
echo "60V bus, 100mΩ supply impedance, 3µH supply loop"          | tee -a "$OUT"
echo "Reference: steady-state V_bus during 30A load = 57V"       | tee -a "$OUT"
echo ""                                                           | tee -a "$OUT"

# label                          c_per  esr_per  esl_per  n
run_config "A_Original_2x150_wet"       150u   0.20    12n     2
run_config "B_Yunrui_4x47_SMD"          47u    0.035   5n      4
run_config "C_Econd_2x100_THT"          100u   0.040   12n     2
run_config "D_JIEPB_4x47_THT"           47u    0.030   12n     4
run_config "E_Panasonic_4x47_SMD"       47u    0.036   5n      4
run_config "F_AllMLCC_moteus_style"     8u     0.001   1n      6   # 6× 1210 X7R 100V/22µF, ~8µF eff each at 60V
run_config "G_Yunrui_x6_extracaps"      47u    0.035   5n      6   # B + 50% more caps

echo ""                                                           | tee -a "$OUT"
echo "Lower V_bus = bigger dip = worse transient response"        | tee -a "$OUT"
echo "Most-relevant time scales for FOC current loop:"            | tee -a "$OUT"
echo "  t+1us:    instantaneous (set by ESL + ESR + MLCC)"        | tee -a "$OUT"
echo "  t+10us:   bulk caps fully engaged, supply still choked"   | tee -a "$OUT"
echo "  t+100us:  current-loop response time (~5kHz BW)"          | tee -a "$OUT"
echo "  min:      worst-case dip during transient"                | tee -a "$OUT"

rm -f tmp_sweep.cir
