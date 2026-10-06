//
// Created by super on 27/10/2025.
//

#ifndef INPUT_H
#define INPUT_H

#include <array>

namespace cpt {

    enum class KEY {
        KEY_Unknown =            -1,
        KEY_Space =               32,
        KEY_Apostrophe =          39,  // '
        KEY_Comma =               44,  // ,
        KEY_Minus =               45,  // -
        KEY_Period =              46,  // .
        KEY_Slash =               47,  // /
        KEY_Num0 =                48,  // 0
        KEY_Num1 =                49,  // 1
        KEY_Num2 =                50,  // 2
        KEY_Num3 =                51,  // 3
        KEY_Num4 =                52,  // 4
        KEY_Num5 =                53,  // 5
        KEY_Num6 =                54,  // 6
        KEY_Num7 =                55,  // 7
        KEY_Num8 =                56,  // 8
        KEY_Num9 =                57,  // 9
        KEY_Semicolon =           59,  // ;
        KEY_Equal =               61,  // =
        KEY_A =                   65,
        KEY_B =                   66,
        KEY_C =                   67,
        KEY_D =                   68,
        KEY_E =                   69,
        KEY_F =                   70,
        KEY_G =                   71,
        KEY_H =                   72,
        KEY_I =                   73,
        KEY_J =                   74,
        KEY_K =                   75,
        KEY_L =                   76,
        KEY_M =                   77,
        KEY_N =                   78,
        KEY_O =                   79,
        KEY_P =                   80,
        KEY_Q =                   81,
        KEY_R =                   82,
        KEY_S =                   83,
        KEY_T =                   84,
        KEY_U =                   85,
        KEY_V =                   86,
        KEY_W =                   87,
        KEY_X =                   88,
        KEY_Y =                   89,
        KEY_Z =                   90,
        KEY_LeftBracket =         91,  // [
        KEY_Backslash =           92,
        KEY_RightBracket =        93,  // ]
        KEY_GraveAccent =         96,  // `
        KEY_World1 =             161, // Non-US #1
        KEY_World2 =             162, // Non-US #2
        KEY_Escape =             256,
        KEY_Enter =              257,
        KEY_Tab =                258,
        KEY_Backspace =          259,
        KEY_Insert =             260,
        KEY_Delete =             261,
        KEY_Right =              262,
        KEY_Left =               263,
        KEY_Down =               264,
        KEY_Up =                 265,
        KEY_PageUp =             266,
        KEY_PageDown =           267,
        KEY_Home =               268,
        KEY_End =                269,
        KEY_CapsLock =           280,
        KEY_ScrollLock =         281,
        KEY_NumLock =            282,
        KEY_PrintScreen =        283,
        KEY_Pause =              284,
        KEY_F1 =                 290,
        KEY_F2 =                 291,
        KEY_F3 =                 292,
        KEY_F4 =                 293,
        KEY_F5 =                 294,
        KEY_F6 =                 295,
        KEY_F7 =                 296,
        KEY_F8 =                 297,
        KEY_F9 =                 298,
        KEY_F10 =                299,
        KEY_F11 =                300,
        KEY_F12 =                301,
        KEY_F13 =                302,
        KEY_F14 =                303,
        KEY_F15 =                304,
        KEY_F16 =                305,
        KEY_F17 =                306,
        KEY_F18 =                307,
        KEY_F19 =                308,
        KEY_F20 =                309,
        KEY_F21 =                310,
        KEY_F22 =                311,
        KEY_F23 =                312,
        KEY_F24 =                313,
        KEY_F25 =                314,
        KEY_KeyPad0 =            320,
        KEY_KeyPad1 =            321,
        KEY_KeyPad2 =            322,
        KEY_KeyPad3 =            323,
        KEY_KeyPad4 =            324,
        KEY_KeyPad5 =            325,
        KEY_KeyPad6 =            326,
        KEY_KeyPad7 =            327,
        KEY_KeyPad8 =            328,
        KEY_KeyPad9 =            329,
        KEY_KeyPadDecimal =      330,
        KEY_KeyPadDivide =       331,
        KEY_KeyPadMultiply =     332,
        KEY_KeyPadSubtract =     333,
        KEY_KeyPadAdd =          334,
        KEY_KeyPadEnter =        335,
        KEY_KeyPadEqual =        336,
        KEY_LeftShift =          340,
        KEY_LeftControl =        341,
        KEY_LeftAlt =            342,
        KEY_LeftSuper =          343,
        KEY_RightShift =         344,
        KEY_RightControl =       345,
        KEY_RightAlt =           346,
        KEY_RightSuper =         347,
        KEY_Menu =               348
    };

    enum class KEY_STATE {
        RELEASED,
        PRESSED,
        REPEAT
    };

    class input {
    public:
        input();

        void updateKeys(KEY keys, KEY_STATE action);
        void updateMousePos(double x, double y);

        bool keyHeld(KEY key) const;

        void getMousePos(double &x, double &y) const;

        void resetKeys();
    private:
        std::array<KEY_STATE, 512> keys;

        double mouseX = 0;
        double mouseY = 0;
    };

} // cpt

#endif //INPUT_H
