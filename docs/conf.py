# Sphinx configuration for the qbar documentation.
# Build: sphinx-build -b html docs docs/_build/html

project = "qbar"
author = "qbar contributors"
copyright = "qbar contributors"
release = "0.1.0"
version = "0.1"

extensions = []

# The repo also keeps standalone Markdown notes in docs/; they are not part of the
# Sphinx (reStructuredText) tree, so exclude them to avoid "not in any toctree" noise.
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store", "*.md"]

master_doc = "index"

html_theme = "alabaster"
html_title = "qbar"
html_static_path = ["assets"]
html_theme_options = {
    "description": "A CSS-themable Qt 6 / QML status bar for Wayland and X11.",
    "page_width": "1040px",
    "fixed_sidebar": True,
}

# Treat warnings seriously when building in CI, but don't fail a local build.
nitpicky = False
