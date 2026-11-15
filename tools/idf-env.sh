# Set up ESP-IDF 5.3.1 in the current shell (zsh or bash):
#   . ~/withernet/tools/idf-env.sh
# ESP-IDF runs on Homebrew's Python 3.13. On the Mac's own Python 3.9 (end of
# life) ESP-IDF's dependency check can't find ruamel.yaml 0.19 by name, and
# idf.py refuses to build.
export PATH="/opt/homebrew/opt/python@3.13/libexec/bin:$PATH"
. "$HOME/esp/esp-idf-v5.3.1/export.sh" > /dev/null
