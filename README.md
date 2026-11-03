# Learning C++ by Building Games: Fundamentals, Second Edition

The code for *Learning C++ by Building Games: Fundamentals — The Core Math and Algorithms Behind Every Game, Built from Scratch in C++*, second edition, by John Horton: its 36 demos, and the skeleton they start from. You'll find the book, and the rest of the *Learning C++ by Building Games* series, at [GameCodeSchool.com](https://gamecodeschool.com).

Looking for the first edition's code? It's still at [Learning-C-by-Building-Games-Fundamentals](https://github.com/EliteIntegrity/Learning-C-by-Building-Games-Fundamentals).

## What's here

There's a folder for each demo, named after it, and a `Skeleton` folder, the program every demo starts from. Each holds the code and a Visual Studio project, with everything set up. SDL 3 and SDL_ttf sit beside the folders, in `SDL3` and `SDL3_ttf`, and so does the font the demos use, `RobotoMono-Light.ttf`.

| Folder | Chapter |
|---|---|
| `Skeleton` | Chapter 1, and Appendix B |
| `Plotter` | 1. Setting the Scene: Coordinates, Points, and Our SDL Toolkit |
| `Vector Lab` | 2. The 2D Vector, and Building Vec2 |
| `Chase` | 3. Vector Arithmetic: Length, Distance, and Direction |
| `Field of View` | 4. The Dot and Cross Products |
| `Which Side` | 4. The Dot and Cross Products |
| `Unit Circle` | 5. Angles, the Unit Circle, and Trigonometry |
| `Rotator` | 6. Rotating Points and Vectors |
| `Thrust` | 7. Motion: Delta Time, Velocity, and Acceleration |
| `Ease Lab` | 8. Interpolation and Easing |
| `Critters` | 9. Steering Behaviors |
| `Bouncy` | 10. Forces of Nature: Gravity, Friction, and Bouncing |
| `Shape Gallery` | 11. Shapes and How We Represent Them |
| `Laser` | 12. Lines, Rays, and Distances |
| `Magnet` | 13. Closest Points |
| `Path Designer` | 14. Curves and Paths: Bezier and Splines |
| `Overlap Playground` | 15. Point and Overlap Tests |
| `SAT Visualizer` | 16. The Separating Axis Theorem |
| `Bullet Time` | 17. Raycasting and Swept Collision |
| `Walls and Slides` | 18. Collision Response |
| `10000 Balls` | 19. Spatial Partitioning: Grids and Quadtrees |
| `Impulse` | 20. A Simple Physics Engine: Mass, Forces, and the Fixed Timestep |
| `Noise Fields` | 21. Randomness and Noise |
| `Flood` | 22. Pathfinding I: Grids, BFS, and Dijkstra |
| `A-Star Explorer` | 23. Pathfinding II: A\* and Beyond |
| `Texture Quad` | 24. Textures and UV Mapping |
| `Solar System` | 25. Transforms and the Scene Graph |
| `Matrix Sandbox` | 26. Matrices for 2D |
| `Camera` | 27. The 2D Camera |
| `Normals` | 28. Thinking in 3D: Coordinates and Vectors |
| `Transforming a Cube` | 29. 3D Transforms and 4 by 4 Matrices |
| `Projector` | 30. Projection: From 3D to a 2D Screen |
| `Fly Camera` | 31. The 3D Camera: View Matrix and Look-At |
| `Mesh Viewer` | 32. The Full Pipeline and Wireframe Rendering |
| `Solid Cube` | 33. Hidden Surfaces: Culling, Sorting and the Z-Buffer |
| `Textured Cube` | 34. Texturing in 3D |
| `Tumble` | 35. Rotations Done Right: Quaternions |

The code in the book is the code in these folders, line for line, so when a chapter says what a demo does, the demo in front of you does it.

## On Windows

Open a demo's `.slnx` file in Visual Studio 2026, choose **Trust and Continue** if Visual Studio asks, and press **F5**. Visual Studio builds the program, copies SDL's two DLLs and the font beside it, and runs it. Each project points at the `SDL3` and `SDL3_ttf` folders at the top of the repository, so it builds wherever you put the repository, as long as the folders stay together.

To start a program of your own, copy the `Skeleton` folder, and keep the copy beside the others. Chapter 1's A Project of Your Own shows how to set up an empty project instead.

## On macOS and Linux

`CMakeLists.txt` builds every demo with CMake. It uses SDL and SDL_ttf if they're installed and new enough, and downloads them the first time if they aren't. Appendix F of the book, For Mac and Linux Users, walks through it with VS Code.

## Licenses

The code is under the license in [LICENSE.md](LICENSE.md). You may use it in your own games and programs, free or commercial, but please don't republish it as teaching material of your own.

The font, `RobotoMono-Light.ttf`, is Roboto Mono, copyright 2015 The Roboto Mono Project Authors, under the Apache License 2.0, which is beside it as `RobotoMono-LICENSE.txt`.

SDL 3 and SDL_ttf are copyright Sam Lantinga, under the zlib license, which is in their folders. SDL_ttf includes FreeType and HarfBuzz, under their own licenses.
