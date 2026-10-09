# Showcase model credits

These three free models appear in the screenshots. The STL files are not part
of the application or the source repository. Download them with
`python3 scripts/download-demo-models.py`, or get the separate demo-model ZIP
from the GitHub release.

| Model | Creator / source | License / terms |
| --- | --- | --- |
| [3DBenchy](https://www.3dbenchy.com/) | Daniel Norée / Creative Tools; [official STL](https://github.com/CreativeTools/3DBenchy/tree/master/Single-part) | [CC0 1.0](https://www.3dbenchy.com/license/) |
| [Stanford Bunny](https://commons.wikimedia.org/wiki/File:Stanford_Bunny.stl) | MakerBot, based on the Stanford bunny scan; file shared on Wikimedia Commons | [CC BY 3.0](https://creativecommons.org/licenses/by/3.0/) |
| [Utah Teapot](https://users.cs.utah.edu/~dejohnso/models/teapot.html) | Martin Newell; STL from David E. Johnson’s University of Utah model repository | [Freely available for any use, including commercial use](https://graphics.cs.utah.edu/teapot/) |

Benchy and Bunny are unmodified. The Utah Teapot’s coordinates and normals
are rotated 90° about X to orient this Y-up source upright in the Z-up demo
collection; its shape and triangle topology are unchanged. Screenshots change
the viewing angle and rendering style. Model creators do not endorse this application. The MIT
license for the viewer does not replace the separate terms for these models.

Screenshots are real app captures: angled shaded surfaces with triangle-edge
overlays, plus the Surface angle mode and settings panel. Reproduce them with:

```sh
python3 scripts/download-demo-models.py
cmake -S . -B build -DBUILD_SHOWCASE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
./build/capture-showcase "$HOME/Downloads/STL-Browser-showcase" docs/showcase
```
