# KodosiUI

Native Linux client for Kodosi: a focused terminal workbench with trusted remote
access, sharing, and lightweight organization.

See [PRODUCT.md](PRODUCT.md) for product intent and [DESIGN.md](DESIGN.md) for the
visual direction.

## Build

Requires Linux x86-64 and the sibling runtime and terminal checkouts.

```sh
just configure
just build
just test
just lint
```

Run `just check` for the complete gate. Bootstrap tools stay under `.tools/` and
build output stays under `build/`.

## License

MIT. See [LICENSE](LICENSE). Third-party notices are under `packaging/licenses/`.
