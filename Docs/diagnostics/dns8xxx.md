# DNS8xxx — Generation and write-back

## DNS8001

An asset name conflicts, or an asset cannot be created or saved.

**Explanation:** Assets are named from source basenames, which must be unique within an asset
directory: `A/Intro.dna` and `B/Intro.dna` conflict. The generator rejects conflicts before touching
assets, and compiling one file cannot overwrite an asset owned by another source. Rename a file or
choose a different asset directory.

Assets remember the source that last generated them. If that source is outside the current source
directory (another machine or checkout) or no longer exists (a moved file), the new source can take
ownership with a warning using this code.

Other causes include an unwritable destination, another editor locking the file, or invalid package
characters. Inspect the reported path, resolve the problem and retry.
