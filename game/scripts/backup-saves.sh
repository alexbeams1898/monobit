#!/usr/bin/env bash
# Backup save data to a persistent location outside the build folder.
# Usage: ./game/scripts/backup-saves.sh [restore]
#
# backup (default): copies build/bin/saves/ -> ~/.prison-break-saves/
# restore:          copies ~/.prison-break-saves/ -> build/bin/saves/

set -euo pipefail

BACKUP_DIR="$HOME/.prison-break-saves"
SAVE_DIR="build/bin/saves"

case "${1:-backup}" in
    backup)
        if [ -d "$SAVE_DIR" ]; then
            mkdir -p "$BACKUP_DIR"
            cp -r "$SAVE_DIR"/* "$BACKUP_DIR"/
            echo "Backed up saves to $BACKUP_DIR"
        else
            echo "No saves found at $SAVE_DIR"
        fi
        ;;
    restore)
        if [ -d "$BACKUP_DIR" ]; then
            mkdir -p "$SAVE_DIR"
            cp -r "$BACKUP_DIR"/* "$SAVE_DIR"/
            echo "Restored saves from $BACKUP_DIR"
        else
            echo "No backup found at $BACKUP_DIR"
        fi
        ;;
    *)
        echo "Usage: $0 [backup|restore]"
        exit 1
        ;;
esac
