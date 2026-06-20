# Calliope

A functional DSL for composing music programmatically.

## Example
The opening theme of Mozart's Rondo alla Turca.
```sh
calliope demo.cal -o demo.wav
```

```calliope
#load "prelude"

-- Mozart, Rondo alla Turca (opening theme)

motif = b'16 a'16 gis'16 a'16           -- the signature turn, around A

theme = motif :+: c''8 :+: r8
        :+: d''16 c''16 b'16 c''16 :+: e''8 :+: r8
        :+: f''16 e''16 dis''16 e''16
        :+: ottava 1 motif :*: 2
        :+: c'''4

main = theme |> meter 2 4 |> tempo 120
```
[`demo.wav`](./demo.wav)
