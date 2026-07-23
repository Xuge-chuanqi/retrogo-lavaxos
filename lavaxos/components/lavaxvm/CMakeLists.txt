set(COMPONENT_SRCS
    "source/file.c"
    "source/font.c"
    "source/gmode16.c"
    "source/hardware.c"
    "source/lava.c"
    "source/lcd.c"
    "source/main.c"
    "source/myctype.c"
    "source/pinyin.c"
    "source/py2gb.c"
    "source/platform.c"
)
set(COMPONENT_ADD_INCLUDEDIRS "include" "source")
set(COMPONENT_REQUIRES "retro-go" "fatfs")
register_component()
rg_setup_compile_options(-O2 -Wno-error=format -Wno-error=pointer-to-int-cast -Wno-error=int-to-pointer-cast -Wno-error=sign-compare)
