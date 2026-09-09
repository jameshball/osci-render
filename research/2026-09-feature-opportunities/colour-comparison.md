# Scene and Editor colour comparison

Compared the alternatives in the running application against the Effects panel. The final combination uses standard grey Scene controls with dark headers and a dark preview, plus the darkest neutral source-editing surface. The Editor's redundant outer group is removed in every editor comparison.

| Scene treatment | Screenshot | Decision |
| --- | --- | --- |
| Standard panel grey, dark preview | [View](screenshots/2026-09-09-colour-study/scene-1.png) | Selected: matches Effects and modulation controls while separating the drawing surface. The final pass also aligns the toolbar header around its buttons. |
| Darker neutral panels | [View](screenshots/2026-09-09-colour-study/scene-2.png) | Calmer, but introduces another panel shade that differs from the rest of the app. |
| Grey throughout | [View](screenshots/2026-09-09-colour-study/scene-3.png) | Consistent palette, but weak separation between the preview and its controls. |

[Effects reference](screenshots/2026-09-09-colour-study/effects-reference.png)

| Editor surface | Screenshot | Decision |
| --- | --- | --- |
| Standard grey | [View](screenshots/2026-09-09-colour-study/editor-1.png) | Too much uninterrupted grey for a large editing area. |
| Medium neutral | [View](screenshots/2026-09-09-colour-study/editor-2.png) | Better separation, but still visually heavy. |
| Dark neutral | [View](screenshots/2026-09-09-colour-study/editor-3.png) | Selected: clearer separation between source content and supporting controls, using colours derived from the app's palette. |

The selected object row was also being painted twice. Removing the duplicate pass softens its green highlight and prevents doubled text rendering. Standalone hides both automation assignment entry points; plugin builds retain them.
