"""Compare compiled planner profiles to independently hashed local baselines."""
import hashlib
from pathlib import Path
from compiled_profiles import profiles
from pe_image import PeImage

BASELINES = {
    'rebirth1': ('NeptuniaReBirth1.exe', '0a6fdae55681019c9612f2da5e59c7539a2def22f66546695b8e9978ca88ab71'),
    'rebirth2': ('NeptuniaReBirth2.exe', 'f0b0b9fe40a26a001d0ac87702d51844f19900cfabe3d38680b885cc607c2fb2'),
    'rebirth3': ('NeptuniaReBirth3.exe', '455ab306a0e44b6e97af1ce2f7c6cf3b56c0352aaa1d62d24fffc2b11088751b'),
    'sega-hard-girls': ('Neptune VSSega Hard Girls.exe', 'af9550b0b5ab90810e31274066f13b3e1b224ef5960951a44d2acd53903523e5'),
}

def main():
    root=Path(__file__).resolve().parents[3]
    expected={
        'rebirth1':(0x138d66,0x26a020,0x1a178,12),
        'rebirth2':(0x1a8d96,0x2b3100,0x1a778,12),
        'rebirth3':(0x1de976,0x2f4e70,0x1c824,12),
        'sega-hard-girls':(0x1a0cff,0x2c07f0,0x2286c,8),
    }
    owned={
        'rebirth1':(0x138c60,0x269e80,0x139270,0x26b2a0,0x139210,0x162bc0,[0x138d50,0x143ac3,0x1422e0,0x138c30]),
        'rebirth2':(0x1a8c90,0x2b2f20,0x1a9290,0x2b4c60,0x1a9230,0x1cf0d0,[0x1a8d80,0x1b3953,0x1b2160,0x1a8c60]),
        'rebirth3':(0x1de870,0x2f4c90,0x1dee70,0x2f6990,0x1dee10,0x206ef0,[0x1de960,0x1e95c3,0x1e7dd0,0x1de840]),
        'sega-hard-girls':(0x1a0c00,0x2c0610,0x1a11f0,0x2c2390,0x1a1190,0x1ced90,[0x1a0ce9,0x1ab7a3,0x1a9e80,0x1a0bd0]),
    }
    for game,(call,start,planner,busy) in expected.items():
        filename,digest=BASELINES[game]
        image=(root/'binaries'/game/filename).read_bytes()
        assert hashlib.sha256(image).hexdigest()==digest
        pe=PeImage(image);p=profiles()[game]['enemy_turn']
        assert (p['call'],p['start'],p['planner'],p['busy'])==(call,start,planner,busy)
        assert pe.mapped(call,16)==bytes(p['bytes'])
        assert pe.call_target(call)==start and pe.mapped(call+12,4)==bytes([0xc6,0x46,busy,1])
        setup=bytes(p['setup']);assert pe.mapped(call-7,7)==setup
        assert (call-7)//4096==(call-1)//4096
        assert setup==(b'\x51\x6a\x04\x50\x89\x75\xec' if game=='rebirth1' else b'\x89\x75\xec\x51\x6a\x04\x50')
        assert pe.mapped(call-10,3)==b'\x8d\x4d\xec'
        producer,creator,callback,trampoline,query,battle,calls=owned[game]
        assert tuple(p[k] for k in ('creator','callback','trampoline','query','battle'))==(creator,callback,trampoline,query,battle)
        assert p['owned_calls']==calls
        assert [pe.call_target(r) for r in calls]==[creator,query,query,battle]
        assert pe.mapped(producer+0x1e,6)==b'\x8d\xb0'+planner.to_bytes(4,'little')
        assert pe.mapped(query+9,5)==b'\x05'+planner.to_bytes(4,'little')
        for r in (callback,trampoline):assert pe.mapped(r,3)==b'\x55\x8b\xec'
        print(game+': compiled planner, setup, continuation, native CALL and page contract passed')
if __name__=='__main__':main()
