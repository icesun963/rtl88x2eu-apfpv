Queue Flush And Forced-Rate Instructions
========================================

The rtl88x2eu driver exposes several procfs helpers under
`/proc/net/rtl88x2eu/<iface>/`. The sections below walk through the two
custom features that were added in this fork.

Queue Flush Control (`flush_tx`)
--------------------------------

- Path: `/proc/net/rtl88x2eu/<iface>/flush_tx`
- Requires root access (`sudo -i` or use `sudo tee`).

Usage examples:

```
# Show available interfaces
ls /proc/net/rtl88x2eu

# Flush only the public queue (PUBQ) without disturbing the link
echo pub | sudo tee /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush specific data queues (multiple tokens are accepted)
echo "vo vi" | sudo tee /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush every queue and cancel active USB transfers
echo all | sudo tee /proc/net/rtl88x2eu/wlan0/flush_tx
```

Tokens you can pass (case-insensitive):

- `all` – flushes every hardware queue; also cancels USB transfers.
- `vo`, `vi`, `be`, `bk` – individual data queues.
- `mgmt`, `hiq`, `hi` – management/high priority queue.
- `pub` – alias for the public queue. This was the original source of
  build-up when PUBQ radio buffers needed to be cleared.
- Numeric queue IDs (`0`–`7`) are also accepted.

Notes:

- Flushing non-data queues (`pub`, `mgmt`, `hiq`) leaves the transport
  path enabled, so the interface stays connected.
- When data queues are flushed (`vo`, `vi`, `be`, `bk`, `all`), the
  driver cancels outstanding URBs and re-enables transmission
  automatically.

Forced-Rate Telemetry (`rate_ctl`, `tx_stat`, `sta_tx_stat`)
------------------------------------------------------------

The driver supports forcing a transmit rate via `rate_ctl`. The
telemetry update that previously ran in the watchdog loop has been
disabled, so you now pull retry/failure counters manually when needed.

Set or clear a forced rate:

```
# Force MCS5 (0x15) with data fallback enabled
echo "0x15 1" | sudo tee /proc/net/rtl88x2eu/wlan0/rate_ctl

# Return to rate adaptation (RA) mode
echo "0xff 0" | sudo tee /proc/net/rtl88x2eu/wlan0/rate_ctl
```

Collect retry statistics on demand:

```
# Dump retry/failure counters for every associated station
cat /proc/net/rtl88x2eu/wlan0/tx_stat

# Query a single station (replace MAC with the peer you care about)
echo "aa:bb:cc:dd:ee:ff" | sudo tee /proc/net/rtl88x2eu/wlan0/sta_tx_stat
cat /proc/net/rtl88x2eu/wlan0/sta_tx_stat
```

Tips:

- Run the `tx_stat` or `sta_tx_stat` commands immediately after forcing
  a rate to capture retries that occurred under the fixed mask.
- If you revert to RA (`rate_ctl` set to `0xff`), the firmware resumes
  managing link-speed selection and the manual telemetry requests will
  still work whenever you need them.

