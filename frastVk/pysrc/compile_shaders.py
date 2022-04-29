import os,sys,subprocess
from argparse import ArgumentParser

def compileSource(sourceFile, type, path="/tmp/shader"):
    with open(sourceFile,'r') as fp: source = fp.read()

    cmd = "glslangValidator --stdin -S {} -V -o {} << END\n".format(type, path) + source + "\nEND"

    res = subprocess.getoutput(cmd)
    if len(res) > 10:
        print(' - Shader from \'{}\' may have failed, output:\n'.format(sourceFile), res)
    #print(' - Result:', res)
    with open(path,'rb') as fp:
        bs = fp.read()
    # bs = bs + bytes([0])
    codeLen = len(bs)

    return ''.join(('\\x{}'.format(hex(b)[2:]) for b in bs)), codeLen

    out = ''
    for i in range(len(bs)//4):
        for j in range(4):
            out += '\\x' + hex(bs[i*4+(3-j)])[2:]
    return out, codeLen

def main():
    parser = ArgumentParser()

    parser.add_argument('--srcFiles', required=True, nargs='+')
    parser.add_argument('--dstName', required=True)
    args = parser.parse_args()

    spirvs = {}

    for file in args.srcFiles:
        print(' - On',file)
        #name = '_'.join(file.rsplit('/',2)[-2:]).replace('.','_')
        # name = '/'.join(file.rsplit('/',2)[-2:])
        name = '/'.join(file.split('/')[2:])

        if '.f.glsl' in file:
            spirvs[name] = compileSource(file, 'frag')
        elif '.v.glsl' in file:
            spirvs[name] = compileSource(file, 'vert')
        elif '.c.glsl' in file:
            spirvs[name] = compileSource(file, 'comp')

    # Now write the file
    with open(args.dstName, 'w') as fp:
        fp.write('#pragma once\n')
        fp.write('#include <cstring>\n\n')
        fp.write('namespace {\n')

        '''
        for name,(code,codeLen) in spirvs.items():
            fp.write('const char* {} = "'.format(name))
            fp.write(code)
            fp.write('";\n')
            fp.write('const size_t {}_len = {};\n\n'.format(name, codeLen))
        '''
        fp.write('constexpr uint32_t _NumShaders = ' + str(len(spirvs)) + ';\n');
        fp.write('const std::pair<std::string, std::string> _compiledShaders[{}]'.format(len(spirvs)) + ' = {' + '\n')
        for i,(name,(code,codeLen)) in enumerate(spirvs.items()):
            fp.write(' { ' + '"{}", '.format(name))
            # fp.write(code[:2])
            fp.write('std::string{"')
            fp.write(code)
            fp.write('",' + str(codeLen) + '}')
            if i != len(spirvs)-1:
                fp.write(' },\n')
            else:
                fp.write(' }\n')
        fp.write('};\n')

        fp.write('} // namespace\n')

    print(' - Wrote {}'.format(args.dstName))

if __name__ == '__main__': main()
