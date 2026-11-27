// ============================================================================
//  PinDialog.cpp  -  Cuadro de diálogo para pedir el PIN del DNIe
// ============================================================================
//
//  La disposición del diálogo (textos, botones, posiciones) está definida en
//  ActiveXDNIe.rc. Aquí están su COMPORTAMIENTO y su aspecto:
//
//    - No deja escribir más caracteres que la longitud máxima indicada.
//    - El botón "Aceptar" está desactivado hasta llegar a la longitud mínima.
//    - No se filtra ningún carácter: el PIN admite cualquier símbolo.
//    - La casilla "Mostrar el PIN" deja ver lo escrito en vez de puntos.
//
//  ¿POR QUÉ UN DIÁLOGO PROPIO?
//  Para que el PIN lo escriba el usuario en una ventana de Windows que la
//  página web no puede ver, rellenar ni falsear, con el texto, el idioma y la
//  longitud del PIN del DNIe. El PIN recogido se presenta a la tarjeta en
//  AsegurarPin (DLL_ADMIN.cpp).
//
//  Aspecto: el de los diálogos actuales de Windows (zona blanca con el texto
//  principal más grande y azul, botones en una franja gris abajo, letra
//  Segoe UI).
// ============================================================================
#include "stdafx.h"
#include "PinDialog.h"
#include "resource.h"

// Módulo de la DLL, donde están el diálogo y el icono (en el proyecto original, g_hInstance)
#define g_hInstance (_AtlBaseModule.GetResourceInstance())

// Datos que comparte el diálogo con AskForPin
struct PinDialogData
{
    std::wstring* pin;
    std::wstring  prompt;
    size_t        minLength;
    size_t        maxLength;
    // Aspecto
    HFONT         titleFont = nullptr;    // texto principal (Segoe UI 12)
    HBRUSH        whiteBrush = nullptr;   // fondo de la zona de arriba
    HBRUSH        footerBrush = nullptr;  // franja de los botones
    int           footerTop = 0;          // dónde empieza la franja (píxeles)
};

// Colores de los diálogos de Windows (TaskDialog)
static const COLORREF TITLE_COLOR  = RGB(0, 51, 153);
static const COLORREF HINT_COLOR   = RGB(96, 96, 96);
static const COLORREF FOOTER_COLOR = RGB(243, 243, 243);
static const COLORREF FOOTER_LINE  = RGB(223, 223, 223);
static const int      FOOTER_TOP_DLU = 92;   // debe coincidir con el .rc (IDD_PIN)

// Prepara la letra del texto principal, los pinceles de fondo y la posición de la franja inferior.
static void SetModernLook(HWND dialog, PinDialogData* data)
{
    HDC screen = GetDC(dialog);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(dialog, screen);

    LOGFONTW lf = {};
    lf.lfHeight = -MulDiv(12, dpi, 72);
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, L"Segoe UI");
    data->titleFont = CreateFontIndirectW(&lf);
    if (data->titleFont)
        SendDlgItemMessageW(dialog, IDC_PIN_TEXT, WM_SETFONT, (WPARAM)data->titleFont, FALSE);

    data->whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    data->footerBrush = CreateSolidBrush(FOOTER_COLOR);

    RECT r = { 0, FOOTER_TOP_DLU, 0, 0 };
    MapDialogRect(dialog, &r);
    data->footerTop = r.top;
}

// Fondo: blanco arriba y franja gris con una línea fina encima para los botones.
static void PaintBackground(HWND dialog, HDC dc, const PinDialogData* data)
{
    RECT client;
    GetClientRect(dialog, &client);
    RECT top = client, footer = client;
    top.bottom = data->footerTop;
    footer.top = data->footerTop;
    FillRect(dc, &top, data->whiteBrush);
    FillRect(dc, &footer, data->footerBrush);
    HPEN pen = CreatePen(PS_SOLID, 1, FOOTER_LINE);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, client.left, data->footerTop, nullptr);
    LineTo(dc, client.right, data->footerTop);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// Activa o desactiva "Aceptar" y actualiza el texto de ayuda según cuántos
// caracteres lleva escritos el usuario.
static void UpdateDialogState(HWND dialog)
{
    const PinDialogData* data = (const PinDialogData*)GetWindowLongPtrW(dialog, DWLP_USER);
    if (data == nullptr)
        return;
    const size_t length = (size_t)GetWindowTextLengthW(GetDlgItem(dialog, IDC_PIN_EDIT));

    EnableWindow(GetDlgItem(dialog, IDOK), length >= data->minLength);

    wchar_t hint[128];
    swprintf_s(hint, L"Entre %u y %u caracteres (lleva %u).",
               (unsigned)data->minLength, (unsigned)data->maxLength, (unsigned)length);
    SetDlgItemTextW(dialog, IDC_PIN_HINT, hint);
}

// Muestra el PIN tal cual o lo oculta con puntos, según la casilla "Mostrar
// el PIN".
//
// Una casilla de texto con ES_PASSWORD tiene un "carácter de contraseña" (el
// punto que se ve en lugar de cada letra). Con EM_SETPASSWORDCHAR se cambia:
// 0 significa "ninguno", es decir, mostrar el texto real. Para volver a
// ocultarlo hay que devolverle el carácter original, que guardamos al crear
// el diálogo en los "datos de usuario" de la propia casilla (GWLP_USERDATA),
// porque depende de la versión de los controles de Windows (● o *).
static void ShowOrHidePin(HWND dialog)
{
    HWND edit = GetDlgItem(dialog, IDC_PIN_EDIT);
    const bool show = IsDlgButtonChecked(dialog, IDC_PIN_SHOW) == BST_CHECKED;

    SendMessageW(edit, EM_SETPASSWORDCHAR, show ? 0 : (WPARAM)GetWindowLongPtrW(edit, GWLP_USERDATA), 0);
    InvalidateRect(edit, nullptr, TRUE);   // El cambio no se ve hasta repintar.
}

// Pone la imagen del candado (recurso IDI_PIN) en el hueco reservado para
// ella en el diálogo.
//
// La queremos de 48 píxeles, pero en una pantalla con la escala de Windows al
// 150 % eso se vería pequeño: hay que multiplicar por la escala. La escala se
// expresa en "puntos por pulgada" (DPI): 96 = 100 %, 144 = 150 %, etc.
static void SetLockImage(HWND dialog)
{
    HDC screen = GetDC(dialog);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(dialog, screen);

    const int size = MulDiv(48, dpi, 96);   // 48 * dpi / 96, con redondeo.

    // LoadImageW elige, de los tamaños que contiene el .ico, el más cercano
    // al pedido, y lo ajusta si no coincide exactamente.
    HICON icon = (HICON)LoadImageW(g_hInstance, MAKEINTRESOURCEW(IDI_PIN), IMAGE_ICON,
                                   size, size, LR_DEFAULTCOLOR);
    if (icon != nullptr)
        SendDlgItemMessageW(dialog, IDC_PIN_ICON, STM_SETICON, (WPARAM)icon, 0);
    // Si fallara la carga, el diálogo funciona igual, solo que sin imagen.
}

// "Procedimiento de diálogo": Windows llama a esta función cada vez que pasa
// algo en el diálogo (se crea, el usuario teclea, pulsa un botón...). Cada
// suceso llega como un "mensaje" (WM_...). Devolvemos TRUE si lo atendemos.
static INT_PTR CALLBACK PinDialogProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        // El diálogo acaba de crearse. 'lParam' es el puntero que pasamos en
        // DialogBoxParamW (la cadena donde dejar el PIN): lo guardamos
        // asociado a la ventana para recuperarlo al pulsar "Aceptar".
        SetWindowLongPtrW(dialog, DWLP_USER, lParam);

        // Texto según la operación («Introduzca el PIN de su DNIe para ...:»).
        SetDlgItemTextW(dialog, IDC_PIN_TEXT, ((PinDialogData*)lParam)->prompt.c_str());

        // Límite de caracteres que admite la casilla.
        SendDlgItemMessageW(dialog, IDC_PIN_EDIT, EM_SETLIMITTEXT, ((PinDialogData*)lParam)->maxLength, 0);

        // Guardamos el carácter con que la casilla oculta el PIN, para poder
        // restaurarlo al desmarcar "Mostrar el PIN" (ver ShowOrHidePin).
        {
            LRESULT passwordChar = SendDlgItemMessageW(dialog, IDC_PIN_EDIT, EM_GETPASSWORDCHAR, 0, 0);
            if (passwordChar == 0)
                passwordChar = L'*';
            SetWindowLongPtrW(GetDlgItem(dialog, IDC_PIN_EDIT), GWLP_USERDATA, (LONG_PTR)passwordChar);
        }

        SetModernLook(dialog, (PinDialogData*)lParam);
        SetLockImage(dialog);
        UpdateDialogState(dialog);
        SetForegroundWindow(dialog);
        // TRUE = que Windows ponga el cursor en el primer control con
        // WS_TABSTOP del .rc, que es la casilla del PIN. (Si se reordenan
        // los controles en el .rc, el cursor empezará en otro sitio.)
        return TRUE;

    case WM_COMMAND:
        // LOWORD(wParam) = qué control; HIWORD(wParam) = qué le ha pasado.
        switch (LOWORD(wParam))
        {
        case IDC_PIN_EDIT:
            if (HIWORD(wParam) == EN_CHANGE)   // El texto de la casilla ha cambiado.
                UpdateDialogState(dialog);
            return TRUE;

        case IDC_PIN_SHOW:
            if (HIWORD(wParam) == BN_CLICKED)   // Se ha marcado o desmarcado la casilla.
            {
                ShowOrHidePin(dialog);
                // Devolvemos el cursor a la casilla del PIN para seguir escribiendo.
                SetFocus(GetDlgItem(dialog, IDC_PIN_EDIT));
            }
            return TRUE;

        case IDOK:
        {
            wchar_t buffer[256] = {};
            GetDlgItemTextW(dialog, IDC_PIN_EDIT, buffer, ARRAYSIZE(buffer));

            std::wstring* pin = ((PinDialogData*)GetWindowLongPtrW(dialog, DWLP_USER))->pin;
            *pin = buffer;

            // Borramos de forma segura la copia del búfer local y vaciamos
            // la casilla. Lo segundo es solo una precaución: Windows no
            // garantiza que la memoria interna de la casilla quede
            // sobrescrita.
            SecureZeroMemory(buffer, sizeof(buffer));
            SetDlgItemTextW(dialog, IDC_PIN_EDIT, L"");

            EndDialog(dialog, IDOK);
            return TRUE;
        }

        case IDCANCEL:   // Botón "Cancelar", tecla Esc o la X de la ventana.
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_ERASEBKGND:
    {
        const PinDialogData* data = (const PinDialogData*)GetWindowLongPtrW(dialog, DWLP_USER);
        if (data == nullptr || data->whiteBrush == nullptr)
            break;
        PaintBackground(dialog, (HDC)wParam, data);
        SetWindowLongPtrW(dialog, DWLP_MSGRESULT, TRUE);
        return TRUE;
    }

    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:   // textos, icono y casilla "Mostrar el PIN" (zona blanca)
    case WM_CTLCOLORBTN:      // botones (franja gris)
    {
        const PinDialogData* data = (const PinDialogData*)GetWindowLongPtrW(dialog, DWLP_USER);
        if (data == nullptr || data->whiteBrush == nullptr)
            break;
        HDC dc = (HDC)wParam;
        HWND control = (HWND)lParam;
        if (message == WM_CTLCOLORBTN)
        {
            SetBkColor(dc, FOOTER_COLOR);
            return (INT_PTR)data->footerBrush;
        }
        SetBkColor(dc, RGB(255, 255, 255));
        if (control == GetDlgItem(dialog, IDC_PIN_TEXT))
            SetTextColor(dc, TITLE_COLOR);
        else if (control == GetDlgItem(dialog, IDC_PIN_HINT))
            SetTextColor(dc, HINT_COLOR);
        return (INT_PTR)data->whiteBrush;
    }

    case WM_DESTROY:
    {
        PinDialogData* data = (PinDialogData*)GetWindowLongPtrW(dialog, DWLP_USER);
        if (data != nullptr)
        {
            if (data->titleFont)   { DeleteObject(data->titleFont);   data->titleFont = nullptr; }
            if (data->whiteBrush)  { DeleteObject(data->whiteBrush);  data->whiteBrush = nullptr; }
            if (data->footerBrush) { DeleteObject(data->footerBrush); data->footerBrush = nullptr; }
        }
        // El diálogo se cierra: liberamos el icono que cargamos en
        // SetLockImage. Un icono cargado con LoadImage sin el indicador
        // LR_SHARED (nuestro caso) hay que destruirlo con DestroyIcon; el
        // control no lo hace por nosotros. (Los cargados con LR_SHARED o
        // con LoadIcon, en cambio, NO deben destruirse.)
        HICON icon = (HICON)SendDlgItemMessageW(dialog, IDC_PIN_ICON, STM_GETICON, 0, 0);
        if (icon != nullptr)
            DestroyIcon(icon);
        break;
    }
    }
    return FALSE;   // Mensaje no atendido: que Windows haga lo habitual.
}

bool AskForPin(HWND ownerWindow, std::wstring& pin)
{
    return AskForPin(ownerWindow, L"firmar", PIN_MIN_LENGTH, PIN_MAX_LENGTH, pin);
}

bool AskForPin(HWND ownerWindow, const std::wstring& purpose, size_t minLength, size_t maxLength,
               std::wstring& pin)
{
    PinDialogData data;
    data.pin = &pin;
    data.prompt = L"Introduzca el PIN de su DNIe para " + purpose;
    data.minLength = minLength ? minLength : PIN_MIN_LENGTH;
    data.maxLength = (maxLength >= data.minLength && maxLength < 256) ? maxLength : PIN_MAX_LENGTH;

    pin.clear();
    pin.reserve(data.maxLength);   // Evita realojos que dejarían copias del PIN en memoria.

    // DialogBoxParamW muestra el diálogo de forma "modal": no vuelve hasta que
    // se cierra. Devuelve el valor que pasamos a EndDialog (IDOK o IDCANCEL).
    const INT_PTR result = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_PIN),
                                           ownerWindow, PinDialogProc, (LPARAM)&data);
    return result == IDOK;
}
