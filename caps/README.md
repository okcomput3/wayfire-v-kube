# Hexagonal flame cap

`flame-hexagon.png` is a reframed version of the supplied artwork, made with the
built-in image generator. `flame-hexagon-original.png` preserves the source.

Use with six horizontal workspaces (six cube faces), Deformation = None,
and cap picture rotation = 0 degrees (top and bottom). The cap UV mapping
uses a flat-top hexagon inscribed in the square texture: its ideal vertices are
(0,.5), (.25,.0669873), (.75,.0669873), (1,.5), (.75,.9330127), (.25,.9330127).
The actual cap mesh supplies the exact six-sided boundary; generated artwork
alignment is approximate, not a precision CAD outline. The existing rounded
edge fade still applies.

Set Top cap picture and/or Bottom cap picture to the absolute path of
`flame-hexagon.png`. Set picture rotations to 0; arbitrary angles will misalign
the hexagonal border. Disable cap bump mapping and 3D bump mapping for the
unmodified artwork, or enable them to add the existing ripple effect.
Cap opacity still controls transparency.

No compositor settings have been changed automatically.
