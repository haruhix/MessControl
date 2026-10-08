"""Cancel only the owned test launched by validate_single_day_network.py."""
import builtins
import unreal as u

validation = getattr(builtins, "_mc_single_day_network_validation", None)
if validation is None:
    u.log("MC_SINGLE_DAY_NETWORK_STOP no owned validation; existing PIE left untouched")
else:
    validation.finish("Cancelled explicitly by the test operator")
