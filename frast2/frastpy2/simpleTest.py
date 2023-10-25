import sys, os, numpy as np
sys.path.append(os.path.join(os.getcwd(), 'build'))

try:
    import frastpy2
except:
    import frastpy2_c as frastpy2
import cv2

if 0:
    fname = '/data/naip/mocoNaip/moco.fft'

    opts = frastpy2.EnvOptions()
    dset =  frastpy2.FlatReaderCached(fname, opts)

    for k,v in dset.iterTiles(12, 3):
        print(' - iter k', k)
        cv2.imshow('img',v)
        cv2.waitKey(11)

    print(' - showing result of getTile() on ', 3458765874251433104)
    v = dset.getTile(3458765874251433104, 1)
    cv2.imshow('img',v)
    cv2.waitKey(0)

def lookAtR(f,u=np.array((0,0,1.))):
    f = f / np.linalg.norm(f)
    r = np.cross(f, u)
    r = r / np.linalg.norm(r)
    u = np.cross(f, r)
    u = u / np.linalg.norm(u)
    return np.copy(np.stack((r,u,f),1),'C')


if 1:
    # Test threed app wrapper.
    appCfg = frastpy2.AppConfig()
    appCfg.w = 512
    appCfg.h = 512
    appCfg.headless = True

    gtCfg = frastpy2.create_gt_app_config(["/data/naip/mdpa/mdpa.fft"], "/data/elevation/srtm/srtm.fft", '')
    app = frastpy2.GtWrapperApp(appCfg,gtCfg)

    camSpec = frastpy2.CameraSpec(
            appCfg.w, appCfg.h,
            45 * np.pi/180)

    t = np.array((1.08218e+06, -4.8396e+06, 4.00709e+06), dtype=np.float32) / 6.378e6

    for i in range(3000):

        t[0] += 10 / 6e6

        rr = app.setCamera(frastpy2.makeSetCameraFromPosRotSpec(
            t, lookAtR(-t), camSpec))

        rr = app.askAndWaitForRender(frastpy2.RenderAction())

        img = rr.getColor()
        cv2.imshow('render image', img)
        cv2.waitKey(30)

    del app
