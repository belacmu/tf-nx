// WiderSet's API on the Switch (what Teuria.WiderSet's GetApi returns on the PC: its IWiderSetModApi). The Switch's wide mode is its own
// (mods/Teuria.WiderSet: the catalog mod over the console's wide set), so the API answers from it. translate.toml [[interop]] maps every
// mod's copy of WiderSet's interface onto this class (TF.EX's TF.EX.Common.Interop.IWiderSetModApi, Teams+'s Teuria.WiderSet one), as
// FortRise's GetApi proxy maps them onto WiderSet's object by member name.
namespace TfexGlue
{
    public class WiderSetApi
    {
        [Cpp("tfex::gen::svc::IsWide()", Include = "tfexgen_services.hpp")] static extern bool SwitchIsWide();
        [Cpp("tfex::gen::svc::SetWide($0)", Include = "tfexgen_services.hpp")] static extern void SwitchSetWide(bool wide);
        [Cpp("tfex::gen::svc::UIXOffset()", Include = "tfexgen_services.hpp")] static extern float SwitchUIXOffset();

        public bool IsWide { get => SwitchIsWide(); set => SwitchSetWide(value); }
        public float UIXOffset => SwitchUIXOffset();
    }
}
