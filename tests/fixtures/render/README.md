# gg-render fixtures

Render specs for the CLI tests in `tests/CMakeLists.txt` (labels `render`).

`bad-tuning.ggst` is a state whose TUNE chunk the build can't load (`render_tuning_bad_state_warns`).
Remake it from the repository root with a built `gg-render`:

```sh
gg-render tests/fixtures/render/render-tuning-save-state.json -o /tmp/x.wav --save-state /tmp/good.ggst
python3 -c 'b=open("/tmp/good.ggst","rb").read(); open("tests/fixtures/render/bad-tuning.ggst","wb").write(b.replace(b" 19\n", b" 99\n"))'
```

That changes the scale's note count from 19 to 99, so tuning-library refuses the text.
