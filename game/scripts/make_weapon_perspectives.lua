-- Generate multiple rotation variants so we can pick the right angles.

local input = app.params["input"]
local output_dir = app.params["output_dir"] or "."
local vscale = tonumber(app.params["vscale"] or "0.5")

if not input then
    print("Error: --script-param input=<path> required")
    return
end

local sprite = app.open(input)
if not sprite then
    print("Error: could not open " .. input)
    return
end

if sprite.colorMode ~= ColorMode.RGB then
    app.command.ChangePixelFormat{ format="rgb" }
end

local name = app.fs.fileTitle(input)
app.fs.makeDirectory(output_dir)

local src = sprite.cels[1].image
local w = src.width
local h = src.height

local function rotateImage(angle_deg)
    local rad = angle_deg * math.pi / 180.0
    local cosR = math.cos(rad)
    local sinR = math.sin(rad)
    local diag = math.ceil(math.sqrt(w * w + h * h))
    local pad = math.ceil((diag - math.min(w, h)) / 2)
    local bw = w + pad * 2
    local bh = h + pad * 2
    local cx = (bw - 1) / 2.0
    local cy = (bh - 1) / 2.0

    local padded = Image(bw, bh, ColorMode.RGB)
    padded:clear()
    for y = 0, h - 1 do
        for x = 0, w - 1 do
            padded:putPixel(x + pad, y + pad, src:getPixel(x, y))
        end
    end

    local rotated = Image(bw, bh, ColorMode.RGB)
    rotated:clear()
    for dy = 0, bh - 1 do
        for dx = 0, bw - 1 do
            local rx = (dx - cx) * cosR + (dy - cy) * sinR + cx
            local ry = -(dx - cx) * sinR + (dy - cy) * cosR + cy
            local sx = math.floor(rx + 0.5)
            local sy = math.floor(ry + 0.5)
            if sx >= 0 and sx < bw and sy >= 0 and sy < bh then
                rotated:putPixel(dx, dy, padded:getPixel(sx, sy))
            end
        end
    end

    local result = Image(w, h, ColorMode.RGB)
    result:clear()
    local ox = math.floor((bw - w) / 2)
    local oy = math.floor((bh - h) / 2)
    for y = 0, h - 1 do
        for x = 0, w - 1 do
            result:putPixel(x, y, rotated:getPixel(x + ox, y + oy))
        end
    end
    return result
end

local function compressV(img)
    local iw = img.width
    local ih = img.height
    local result = Image(iw, ih, ColorMode.RGB)
    result:clear()
    local ch = math.floor(ih * vscale)
    local yOff = math.floor((ih - ch) * 0.5)
    for outY = 0, ch - 1 do
        local srcY = math.floor(outY * ih / ch)
        if srcY >= 0 and srcY < ih then
            for x = 0, iw - 1 do
                local dy = outY + yOff
                if dy >= 0 and dy < ih then
                    result:putPixel(x, dy, img:getPixel(x, srcY))
                end
            end
        end
    end
    return result
end

local function saveImage(img, filename)
    local spr = Sprite(w, h, ColorMode.RGB)
    spr.cels[1].image = img
    spr:saveCopyAs(filename)
    print("Saved: " .. filename)
    spr:close()
end

-- Generate variants at every 45 degrees, with and without compression.
for deg = 0, 315, 45 do
    local rotated = rotateImage(deg)
    saveImage(rotated, output_dir .. "/" .. name .. "_rot" .. deg .. ".png")
    local compressed = compressV(rotated)
    saveImage(compressed, output_dir .. "/" .. name .. "_rot" .. deg .. "_squish.png")
end

sprite:close()
print("Done - check all variants and pick the best angles")
