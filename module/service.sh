#!/system/bin/sh
# The daemon waits for the ring and survives disconnects by itself;
# this loop only restarts it if it ever crashes.
MODDIR=${0%/*}
while true; do
    "$MODDIR/ring_mapper" -l "$MODDIR/log.txt" "$MODDIR/config.txt"
    # 4 = already running (e.g. started from the app): check again much later
    [ $? -eq 4 ] && sleep 60 || sleep 5
done &
