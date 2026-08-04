-- Mirror a sprite horizontally, in place, in its own .aseprite file.
--
--     aseprite -b art/characters/foo.aseprite --script tools/flip.lua
--
-- For when a character was drawn facing the wrong way. The project convention is that art
-- is drawn FACING RIGHT and the game mirrors it to face left (see Player.cpp) -- so this
-- exists to correct a source file once, not to be part of the build.
--
-- It flips every cel on every layer and frame, which is what "the character faces the other
-- way" means; flipping the canvas alone would leave layers out of register.

local sprite = app.sprite
if not sprite then
    print("flip: no sprite open")
    return
end

-- Every cel on every layer, layers left intact. Flattening would mirror the art correctly and
-- destroy the authoring structure doing it -- an outline layer and a body layer merged into one
-- is not recoverable, and the loss would not be noticed until someone next tried to edit them.
app.transaction("Flip horizontally", function()
    for _, cel in ipairs(sprite.cels) do
        local image = Image(cel.image)
        image:flip(FlipType.HORIZONTAL)
        cel.image = image
        -- A trimmed cel sits at an offset; mirroring the pixels without mirroring that
        -- offset would slide the character sideways within the frame.
        cel.position = Point(sprite.width - cel.position.x - cel.image.width, cel.position.y)
    end
end)

sprite:saveAs(sprite.filename)
print("flip: " .. sprite.filename)
