# Image Background

Rebuilds a picture out of editor objects, so it can sit behind your level.

1. Open the editor and press **IMG Folder**. Put a `.png` or `.jpg` picture into the folder that opens.
2. Press **IMG to BG**. The newest picture in that folder is rebuilt from coloured squares in the middle of your view.
3. The squares are selected, so you can move or scale the whole picture right away. Undo removes it.

The squares use the built-in white colour channel with a per-object HSV tint, so no colour channels are used up. They are on the lowest Z layer and marked NoTouch, so the player passes through them.

Settings (**IMG Settings**): width in cells, cell size, colour detail, group ID, editor layer.
