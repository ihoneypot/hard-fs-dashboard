SET(SOURCES components/ui_comp_label1.c
    components/ui_comp.c
    screens/ui_MainScreen.c
    screens/ui_DebugScreen.c
    ui.c
    components/ui_comp_hook.c
    ui_helpers.c
    images/ui_img_rectangle_27_png.c
    images/ui_img_subtract_png.c
    images/ui_img_rectangle_24_png.c
    images/ui_img_debugframe_png.c
    fonts/ui_font_TomorrowBoldItalic12.c
    fonts/ui_font_TomorrowBoldItalic14.c
    fonts/ui_font_TomorrowBoldItalic18.c
    fonts/ui_font_TomorrowBoldItalic26.c
    fonts/ui_font_TomorrowBoldItalic64.c)

add_library(ui ${SOURCES})
