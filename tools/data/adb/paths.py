from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent.parent
REPO_DIR = TOOLS_DIR.parent.parent
CACHE_DIR = TOOLS_DIR / ".cache"
DATA_DIR = REPO_DIR / "data"
REPORTS_DIR = TOOLS_DIR / "reports"

MASTER_YAML = TOOLS_DIR / "drinks_master.yaml"
INGREDIENTS_YAML = TOOLS_DIR / "ingredients.yaml"
TAGS_PROPOSED_YAML = TOOLS_DIR / "tags_proposed.yaml"
TAGS_OVERRIDES_YAML = TOOLS_DIR / "tags_overrides.yaml"
PHOTO_OVERRIDES_YAML = TOOLS_DIR / "photo_overrides.yaml"
