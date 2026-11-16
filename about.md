# Image Background

Rebuilds a picture out of editor objects, so it can sit behind your level.

1. Open the editor and press **IMG Folder**. Put a `.png` or `.jpg` picture into the folder that opens.
2. Press **IMG to BG**. The newest picture in that folder is repainted in the middle of your view with overlapping brush strokes (rotated ellipses), big strokes first and fine detail last, so it looks like a painting rather than pixel art.
3. The objects are selected, so you can move or scale the whole picture right away. Undo removes it.

The objects use the built-in white colour channel with a per-object HSV tint, so no colour channels are used up. They are on the lowest Z layer and marked NoTouch, so the player passes through them.

Settings (**IMG Settings**): style (painting or pixels), number of strokes, picture width, group ID, editor layer.
