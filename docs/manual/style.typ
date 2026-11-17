// Shared page design for the SONICATOR user manual.
#let accent = rgb("#c2611c")
#let accent-soft = rgb("#f3e3d6")
#let ink = rgb("#1a1b1d")
#let muted = rgb("#63666a")
#let rule = rgb("#d4d7db")
#let tint = rgb("#f3f4f6")
#let plate-dark = rgb("#17181a")
#let warning-red = rgb("#a33a2a")

#let body-font = "Manrope"
#let mono-font = "DejaVu Sans Mono"

#set document(title: "SONICATOR User Manual", author: "Ilya Orange")

#let book(body) = {
  set page(
    paper: "a4",
    margin: (top: 22mm, bottom: 24mm, inside: 22mm, outside: 26mm),
    footer: context {
      set text(font: body-font, size: 7.5pt, fill: muted)
      grid(
        columns: (1fr, auto),
        align: (left, right),
        [SONICATOR · User manual],
        counter(page).display(),
      )
    },
  )

  set text(font: (body-font, mono-font), size: 10.5pt, fill: ink, lang: "en", region: "GB")
  set par(justify: true, leading: 0.72em, spacing: 0.72em)
  set block(spacing: 1em)
  set heading(numbering: none)

  // ---- Headings -------------------------------------------------------------

  show heading.where(level: 1): it => {
    pagebreak(weak: false)
    block(above: 0pt, below: 16pt)[
      #set text(size: 24pt, weight: "bold", fill: ink, tracking: -0.4pt)
      #it.body
    ]
  }

  show heading.where(level: 2): it => {
    block(above: 22pt, below: 8pt)[
      #set text(size: 14.5pt, weight: "bold", fill: ink)
      #it.body
    ]
  }

  show heading.where(level: 3): it => {
    block(above: 12pt, below: 4pt)[
      #set text(size: 11pt, weight: "bold", fill: ink)
      #it.body
    ]
  }

  body
}

// ---- Parts ----------------------------------------------------------------

#let section(title) = heading(level: 2, outlined: false)[#title]

#let part(number, title, summary) = {
  pagebreak(weak: false)
  page(footer: none, background: rect(fill: plate-dark, width: 100%, height: 100%), margin: (top: 40mm, bottom: 30mm, x: 26mm))[
    #set text(fill: white)
    #block(above: 0pt)[
      #text(size: 10pt, fill: accent, weight: "bold", tracking: 2pt)[PART #number]
      #v(18pt)
      #text(size: 34pt, weight: "bold", tracking: -0.6pt)[#title]
      #v(14pt)
      #line(length: 30mm, stroke: 1.2pt + accent)
      #v(14pt)
      #text(size: 12pt, fill: rgb("#c9cbcf"))[#summary]
    ]
  ]
}

// ---- Chapter opener -------------------------------------------------------

#let chapter(number, title, lead: none, inthis: (), body) = {
  heading(level: 1)[#text(fill: accent)[#number] #title]
  if lead != none {
    block(above: 4pt, below: 12pt)[
      #set text(size: 12pt, fill: muted)
      #set par(leading: 0.7em)
      #lead
    ]
  }
  if inthis.len() > 0 {
    block(
      fill: tint,
      inset: (x: 12pt, y: 10pt),
      radius: 3pt,
      above: 0pt,
      below: 16pt,
    )[
      #set text(size: 9pt)
      #text(weight: "bold", tracking: 0.8pt)[IN THIS CHAPTER]
      #v(4pt)
      #for item in inthis [• #item \ ]
    ]
  }
  body
}

// ---- Text helpers ---------------------------------------------------------

#let ui(name) = strong(name)
#let path(text) = raw(text, lang: "path")
#let cmd(text) = raw(text)

#let note(body) = block(
  fill: tint,
  stroke: (left: 2pt + rgb("#9aa0a6")),
  inset: (x: 12pt, y: 9pt),
  radius: 2pt,
  above: 10pt,
  below: 10pt,
)[
  #set text(size: 9.5pt)
  #strong("Note. ") #body
]

#let tip(body) = block(
  fill: rgb("#fbf1e8"),
  stroke: (left: 2pt + accent),
  inset: (x: 12pt, y: 9pt),
  radius: 2pt,
  above: 10pt,
  below: 10pt,
)[
  #set text(size: 9.5pt)
  #text(fill: accent, weight: "bold")[Tip.] #body
]

#let warning(body) = block(
  fill: rgb("#f9eeec"),
  stroke: (left: 2pt + warning-red),
  inset: (x: 12pt, y: 9pt),
  radius: 2pt,
  above: 10pt,
  below: 10pt,
)[
  #set text(size: 9.5pt)
  #text(fill: warning-red, weight: "bold")[Warning.] #body
]

#let keys(..names) = {
  let items = names.pos()
  box(
    for (i, name) in items.enumerate() {
      if i > 0 { box(width: 3pt) }
      box(
        inset: (x: 4pt, y: 1pt),
        radius: 2.5pt,
        stroke: 0.6pt + rule,
        fill: white,
        text(size: 8.5pt, weight: "bold")[#name],
      )
    },
  )
}

// ---- Figures --------------------------------------------------------------

#let shot(path, caption, width: 100%, inset: 0pt) = block(
  above: 12pt,
  below: 12pt,
  breakable: false,
)[
  #block(
    stroke: 0.6pt + rule,
    inset: inset,
    radius: 2pt,
    clip: true,
    width: 100%,
  )[#image(path, width: width)]
  #v(4pt)
  #set text(size: 8.5pt, fill: muted)
  #set par(leading: 0.6em)
  #caption
]

#let plate(path, caption) = page(
  flipped: true,
  margin: (top: 14mm, bottom: 14mm, x: 18mm),
  footer: none,
)[
  #image(path, width: 94%)
  #v(4pt)
  #set text(size: 8.5pt, fill: muted)
  #caption
]

#let data-table(header: (), rows: (), widths: auto) = {
  let cell(value) = if type(value) == str { eval(value, mode: "markup") } else { value }
  let render(line) = line.map(cell)
  block(breakable: false)[
    #set par(justify: false)
    #table(
    columns: widths,
    stroke: (x, y) => if y == 0 { 0.8pt + ink } else { 0.4pt + rule },
    inset: (x: 6pt, y: 4pt),
    align: left + top,
    table.header(..render(header)),
    ..rows.map(render).flatten(),
    )
  ]
}

#let source(text) = block(above: 8pt)[
  #set text(size: 8pt, fill: muted)
  Source: #text
]

// ---- Cover ----------------------------------------------------------------

#let cover(version: "", commit: "") = page(
  footer: none,
  background: rect(fill: plate-dark, width: 100%, height: 100%),
  margin: 0pt,
)[
  #set text(fill: white)
  #place(top + left, dx: 26mm, dy: 30mm)[
    #image("/resources/branding/icon-glossy-chevron-1024.png", width: 30mm)
  ]
  #place(top + left, dx: 26mm, dy: 72mm)[
    #image("/resources/branding/logo-silver.png", width: 86mm)
  ]
  #place(bottom + left, dx: 26mm, dy: -30mm)[
    #text(size: 44pt, weight: "bold", tracking: -1pt, fill: white)[User Manual]
    #v(6pt)
    #text(size: 13pt, fill: rgb("#c9cbcf"))[
      From a folder of samples to a working library
    ]
    #v(14pt)
    #line(length: 34mm, stroke: 1.4pt + accent)
    #v(14pt)
    #text(size: 9pt, fill: rgb("#a3a5a9"))[Version #version · #commit]
  ]
]