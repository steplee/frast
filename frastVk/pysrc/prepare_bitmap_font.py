import numpy as np, cv2, os, sys
import freetype

def escape(x):
    if x == "'": return '\\\''
    return x

def main():
    size = 32
    chars = '''0123456789. abcedfghijlkmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ?!@#$%&*\'"()[]<>'''
    maps = []

    face = freetype.Face("/usr/share/fonts/truetype/nerd/Agave-Regular.ttf")
    #face = freetype.Face("/usr/share/fonts/truetype/nerd/3270 Narrow Nerd Font Complete Mono.ttf")
    #face = freetype.Face("/usr/share/fonts/truetype/ubuntu/Ubuntu-M.ttf")
    face.set_char_size(48*64)

    # First pass: find maximum size
    max_size = (0,0)
    for c in chars:
        face.load_char(c)
        bitmap = face.glyph.bitmap
        max_size = max(max_size[0],bitmap.rows), max(max_size[1], bitmap.width)

    img = np.zeros(max_size, dtype=np.uint8)

    outSize = (32,20)

    imgs = []

    for c in chars:
        face.load_char(c)
        bitmap = face.glyph.bitmap
        img[:] = 0

        if bitmap.rows == 0: nimg = np.zeros(outSize,dtype=np.uint8)
        else: nimg = np.array(list(bitmap.buffer),dtype=np.uint8).reshape(bitmap.rows,-1)

        img[-nimg.shape[0]:, :nimg.shape[1]] = nimg
        print(nimg.shape)

        img2 = cv2.resize(img, outSize[::-1])
        # cv2.imshow('char',img2)
        # cv2.waitKey(1)
        imgs.append(img2)

    cols = 16
    rows = (len(imgs) + cols-1) // 16
    fimg = np.zeros((rows*outSize[0], cols*outSize[1]), dtype=np.uint8)
    for i,img in enumerate(imgs):
        y,x = i//cols, i%cols
        fimg[y*outSize[0]:(y+1)*outSize[0], x*outSize[1]:(x+1)*outSize[1]] = img
    cv2.imshow('full',fimg)
    cv2.waitKey(0)


    with open('font.hpp', 'w') as fp:
        fp.write('#pragma once\n')
        fp.write('#include <cstring>\n\n')
        fp.write('namespace {\n')

        # Multi-array version
        '''
        fp.write('constexpr int32_t _numChars = {};\n'.format(len(chars)))
        fp.write('constexpr int32_t _charIndices[' + str(len(chars)) + '] = {' + ','.join(("'{}'".format(escape(c)) for c in chars)) + '};\n')
        fp.write('constexpr char* _charImages[' + str(len(chars)) + '] = {\n')
        for img in imgs:
            fp.write('(char*)"')
            out = ''
            for c in img.reshape(-1):
                out += '\\x' + hex(c)[2:]
            fp.write(out)
            fp.write('",\n')
        fp.write('};\n')
        '''

        charInds = {}
        for i in range(256): charInds[i] = chars.index('?')
        for i,c in enumerate(chars): charInds[ord(c)] = i

        fp.write('constexpr int32_t _numChars = {};\n'.format(len(chars)))
        #fp.write('constexpr int32_t _charIndices[' + str(len(chars)) + '] = {' + ','.join(("'{}'".format(escape(c)) for c in chars)) + '};\n')
        fp.write('constexpr int32_t _charIndices[' + '256' + '] = {' + ','.join(("{}".format(charInds[i]) for i in range(256))) + '};\n')
        fp.write('constexpr int32_t _texHeight = {};\n'.format(outSize[0]))
        fp.write('constexpr int32_t _texWidth = {};\n'.format(outSize[1]))
        fp.write('constexpr int32_t _rows = {};\n'.format(rows))
        fp.write('constexpr int32_t _cols = {};\n'.format(cols))
        fp.write('const char* _image = (char*)"')
        out = ''
        for c in fimg.reshape(-1):
            out += '\\x' + hex(c)[2:]
        fp.write(out)
        fp.write('";\n')

        fp.write('} // namespace\n')






main()
