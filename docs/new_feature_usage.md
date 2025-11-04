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
printf 'pub\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush PUBQ and force-cancel outstanding URBs (legacy behaviour)
printf 'pub cancel\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush specific data queues (multiple tokens are accepted)
printf 'vo vi\n' > /proc/net/rtl88x2eu/wlan0/flush_tx

# Flush every queue, pausing TX briefly
printf 'all\n' > /proc/net/rtl88x2eu/wlan0/flush_tx
```

Tokens you can pass (case-insensitive):

- `all` – flushes every hardware queue; also cancels USB transfers.
- `vo`, `vi`, `be`, `bk` – individual data queues.
- `mgmt`, `hiq`, `hi` – management/high priority queue.
- `pub` – alias for the public queue. This was the original source of
  build-up when PUBQ radio buffers needed to be cleared.
- `cancel` – optionally force a USB bulk-out cancel after the flush.
- Numeric queue IDs (`0`–`7`) are also accepted.

Notes:

- Flushing non-data queues (`pub`, `mgmt`, `hiq`) leaves the transport
  path enabled, so the interface stays connected. Add the `cancel` token
  if you explicitly want to tear down outstanding URBs afterwards.
- When data queues are flushed (`vo`, `vi`, `be`, `bk`, `all`), the
  driver pauses TX, cancels outstanding URBs, and re-enables
  transmission automatically after a short delay.

Forced-Rate Telemetry (`rate_ctl`, `tx_stat`, `sta_tx_stat`)
------------------------------------------------------------

The driver supports forcing a transmit rate via `rate_ctl`. The
telemetry update that previously ran in the watchdog loop has been
disabled, so you now pull retry/failure counters manually when needed.

Set or clear a forced rate:

```
# Force MCS5 (0x15) without firmware fallback (default)
printf '0x15 0\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl

# Force MCS5 (0x15) but allow the firmware to fall back on retries
printf '0x15 1\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl

# Return to rate adaptation (RA) mode
printf '0xff 0\n' > /proc/net/rtl88x2eu/wlan0/rate_ctl
```

Collect retry statistics on demand:

```
# Dump retry/failure counters for every associated station
cat /proc/net/rtl88x2eu/wlan0/tx_stat

# Query a single station (replace MAC with the peer you care about)
printf 'aa:bb:cc:dd:ee:ff\n' > /proc/net/rtl88x2eu/wlan0/sta_tx_stat
cat /proc/net/rtl88x2eu/wlan0/sta_tx_stat
```

Tips:

- Run the `tx_stat` or `sta_tx_stat` commands immediately after forcing
  a rate to capture retries that occurred under the fixed mask.
- "Fallback" controls whether the firmware may walk its retry table and
  drop to lower data rates after the initial attempt at the forced rate
  fails. Leaving fallback enabled gives the hardware room to recover
  from momentary fades or interference without dropping the link;
  disabling fallback means every retry uses exactly the rate you forced.
- Fallback stays disabled unless you explicitly write `1` as the second
  argument. Keep it disabled if you are experimenting with pure fixed
  rates, but enable it when you want retries to walk down the firmware's
  rate table automatically.
- The default `pub` flush keeps USB transports running so even tight
  masks remain stable. If you need the legacy "drop everything" flush,
  add the `cancel` token to explicitly tear down the bulk-out pipes.
- If you revert to RA (`rate_ctl` set to `0xff`), the firmware resumes
  managing link-speed selection and the manual telemetry requests will
  still work whenever you need them.
