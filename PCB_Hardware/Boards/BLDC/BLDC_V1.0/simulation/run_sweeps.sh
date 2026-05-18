#!/bin/bash
# Sweep V_bus and IDRIVE for the half-bridge sim.
# Generates a results table for the README.

set -e
cd "$(dirname "$0")"

OUT=results.txt
> "$OUT"

run_case() {
    local label="$1"
    local vbus="$2"
    local iload="$3"
    local idrivep="$4"
    local idriven="$5"

    sed -e "s/^\.param vbus_v.*/.param vbus_v   = $vbus/" \
        -e "s/^\.param iload_a.*/.param iload_a  = $iload/" \
        -e "s/^\.param idrivep_a.*/.param idrivep_a = ${idrivep}m/" \
        -e "s/^\.param idriven_a.*/.param idriven_a = ${idriven}m/" \
        half_bridge.cir > sweep_tmp.cir

    # Run, extract the three power numbers
    local out
    out=$(timeout 180 ngspice -b sweep_tmp.cir 2>/dev/null | \
          grep -E '^p_(hs|ls|fet_total)_avg' | head -3 | \
          awk '{print $3}' | tr '\n' ' ')

    local p_hs p_ls p_total
    read -r p_hs p_ls p_total <<< "$out"

    printf "%-50s  P_HS=%6.3fW  P_LS=%6.3fW  Total=%6.3fW\n" \
           "$label" "$p_hs" "$p_ls" "$p_total" | tee -a "$OUT"
}

echo "===== V_BUS sweep at 20A, Medium IDRIVE (500/1000 mA) =====" | tee -a "$OUT"
run_case "Vbus=24V, 20A, Med IDRIVE"  24 20 500 1000
run_case "Vbus=48V, 20A, Med IDRIVE"  48 20 500 1000
run_case "Vbus=72V, 20A, Med IDRIVE"  72 20 500 1000
echo "" | tee -a "$OUT"

echo "===== IDRIVE sweep at 48V/20A =====" | tee -a "$OUT"
run_case "Vbus=48V, 20A, Slow  (150/300 mA)"   48 20 150  300
run_case "Vbus=48V, 20A, Med   (500/1000 mA)"  48 20 500 1000
run_case "Vbus=48V, 20A, Fast  (1000/2000 mA)" 48 20 1000 2000
echo "" | tee -a "$OUT"

echo "===== Current sweep at 48V, Medium IDRIVE =====" | tee -a "$OUT"
run_case "Vbus=48V, 10A, Med IDRIVE"   48 10 500 1000
run_case "Vbus=48V, 20A, Med IDRIVE"   48 20 500 1000
run_case "Vbus=48V, 30A, Med IDRIVE"   48 30 500 1000
echo "" | tee -a "$OUT"

rm -f sweep_tmp.cir
echo ""
echo "Results saved to $OUT"
