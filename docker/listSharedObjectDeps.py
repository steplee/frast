import os, sys, subprocess, re, shutil

#
# Script that copies shared-object dependencies to a specified dir.
# The path to input files are not retained, that is the output dir is flat.
#

exclude_substrings = ['libpthread', 'libdl.so' 'librt.so', 'libc.so', 'libm.so']

for arg in sys.argv:
    if arg == '-h' or arg == '--help':
        print(' - Usage:\n\tpython3 listSharedObjectDeps.py <outBasePath> <file1> [<files>]')
assert(len(sys.argv) > 2)

out_base_path = sys.argv[1]

#for fi in sys.argv:
res = subprocess.getoutput('ldd ' + ' '.join(sys.argv[2:]))
deps = re.findall('=> (\S+)', res)
deps = list(set(deps))
print(deps)


failures = []
for dep in deps:
    dep_name = dep.rsplit('/',1)[-1]
    out_path = os.path.join(out_base_path, dep_name)

    for ss in exclude_substrings:
        if ss in dep: continue

    try:
        shutil.copy(dep, out_path, follow_symlinks=True)
    except:
        failures.append((dep, dep_name))

if len(failures) > 0:
    print(' - Note: had {} ldd copy fails.'.format(len(failures)))
    for (dep,name) in failures:
        print("\t- failed '{}'".format(dep))
