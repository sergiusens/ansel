#include "accelerators.h"



guint dt_accels_keypad_alternatives(const guint key_val)
{
  guint alt_char = key_val;
  switch(key_val)
  {
    case GDK_KEY_0:
      alt_char = GDK_KEY_KP_0;
      break;
    case GDK_KEY_1:
      alt_char = GDK_KEY_KP_1;
      break;
    case GDK_KEY_2:
      alt_char = GDK_KEY_KP_2;
      break;
    case GDK_KEY_3:
      alt_char = GDK_KEY_KP_3;
      break;
    case GDK_KEY_4:
      alt_char = GDK_KEY_KP_4;
      break;
    case GDK_KEY_5:
      alt_char = GDK_KEY_KP_5;
      break;
    case GDK_KEY_6:
      alt_char = GDK_KEY_KP_6;
      break;
    case GDK_KEY_7:
      alt_char = GDK_KEY_KP_7;
      break;
    case GDK_KEY_8:
      alt_char = GDK_KEY_KP_8;
      break;
    case GDK_KEY_9:
      alt_char = GDK_KEY_KP_9;
      break;
    case GDK_KEY_Left:
      alt_char = GDK_KEY_KP_Left;
      break;
    case GDK_KEY_Right:
      alt_char = GDK_KEY_KP_Right;
      break;
    case GDK_KEY_Up:
      alt_char = GDK_KEY_KP_Up;
      break;
    case GDK_KEY_Down:
      alt_char = GDK_KEY_KP_Down;
      break;
    case GDK_KEY_Home:
      alt_char = GDK_KEY_KP_Home;
      break;
    case GDK_KEY_End:
      alt_char = GDK_KEY_KP_End;
      break;
    case GDK_KEY_Insert:
      alt_char = GDK_KEY_KP_Insert;
      break;
    case GDK_KEY_Return:
      alt_char = GDK_KEY_KP_Enter;
      break;
    case GDK_KEY_Page_Up:
      alt_char = GDK_KEY_KP_Page_Up;
      break;
    case GDK_KEY_Page_Down:
      alt_char = GDK_KEY_KP_Page_Down;
      break;
    default:
      break;
  }

  return alt_char;
}
