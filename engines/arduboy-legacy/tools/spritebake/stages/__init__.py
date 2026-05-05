"""Stage modules — imported for their side-effect of registering stages.

Importing tools.spritebake.stages pulls in every file below, which
registers each stage in the pipeline.STAGES dict via the @register_stage
decorator. Any new stage file must be listed here.
"""

from . import crop        # noqa: F401
from . import filter as _filter  # noqa: F401  (avoid shadowing builtins.filter)
from . import threshold   # noqa: F401
from . import morph       # noqa: F401
from . import dither      # noqa: F401
from . import silhouette  # noqa: F401
