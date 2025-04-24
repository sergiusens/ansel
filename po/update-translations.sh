
# Update from source code
intltool-update -m
intltool-update -p -g ansel

# Remove old translations
for f in *.po ; do
  echo "$f"
  msgmerge -U $f ansel.pot
  msgattrib --translated --no-obsolete --no-fuzzy -o $f $f
done

# Report
intltool-update -g ansel -r
