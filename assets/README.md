# Weapon silhouettes

The 34 firearm SVGs were extracted from the user's installed CS2 package, under panorama/images/icons/equipment. Item IDs were checked against that installation's scripts/items/items_game.txt. The source paths and SVG hashes are in weapons/manifest.json. Unknown items, knives and grenades have no firearm icon.

weapon-atlas.png contains those silhouettes with transparent padding. CMake embeds the atlas in the DLL, so there are no extra image files to install beside the runtime. The atlas is decoded once per renderer, uploaded once, and reused for every frame.

Normal builds require no image tooling. To regenerate after intentionally changing SVGs or the catalog, run scripts/generate-weapon-atlas.cjs with Node.js and sharp installed. This also regenerates src/weapon_catalog.hpp. Valve-Assets-NOTICE.txt describes asset ownership.
