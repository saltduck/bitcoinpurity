# Seeds

Utility to generate the seeds.txt list that is compiled into the client
(see [src/chainparamsseeds.h](/src/chainparamsseeds.h) and other utilities in [contrib/seeds](/contrib/seeds)).

Be sure to update `PATTERN_AGENT` in `makeseeds.py` to include the current version,
and remove old versions as necessary (at a minimum when SeedsServiceFlags()
changes its default return value, as those are the services which seeds are added
to addrman with).

Purity mainnet uses the manually selected endpoints in `nodes_main.txt`.
Keep that list when refreshing other networks; do not replace it with upstream
crawler results. Domain entries are recorded as numeric addresses with comments
identifying the hostname and resolution date. Refresh those mappings explicitly
when their addresses change, then run `generate-seeds.py` to update the header.

Update `MIN_BLOCKS` in  `makeseeds.py` and the `-m`/`--minblocks` arguments below, as needed.

The other networks' seeds compiled into the release are created from achow101's
DNS seeds and asmap community AS map data. Run the following commands
from the `/contrib/seeds` directory:

```
curl https://signet.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_signet.txt
curl https://testnet.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_test.txt
curl https://testnet4.achownodes.xyz/seeds.txt.gz | gzip -dc > seeds_testnet4.txt
curl https://raw.githubusercontent.com/asmap/asmap-data/main/latest_asmap.dat > asmap-filled.dat
python3 makeseeds.py -a asmap-filled.dat -s seeds_signet.txt -m 237800 > nodes_signet.txt
python3 makeseeds.py -a asmap-filled.dat -s seeds_test.txt > nodes_test.txt
python3 makeseeds.py -a asmap-filled.dat -s seeds_testnet4.txt -m 72600 > nodes_testnet4.txt
python3 generate-seeds.py . > ../../src/chainparamsseeds.h
```
