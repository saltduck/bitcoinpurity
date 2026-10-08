# Wallet transaction-removal RPC contract

`removeprunedfunds txid` continues to delete the specified wallet transaction
and return null. Missing wallet transactions retain error -4. Transaction
history and balances change according to the existing deletion behavior.

After deleting a replaced transaction, listunspent(minconf=0) must continue to
exclude every input spent by a surviving wallet transaction, before and after
confirmation and wallet reload. Removing the sole spend removes its input
associations as before. No RPC arguments, serialization, database schemas or
replacement rules change.
