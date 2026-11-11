# Hub vending showcase

The display fits inside the inset of `SM_BusterShop`. Its four cards show the local player's shop stock and prices. The next page replaces the cards in sequence under a short scan sweep. An offer without an icon displays its name. Press E to open the normal shop.

Unreal Editor Live Coding succeeded. Solo PIE on Hub verified two distinct pages against all eight offers in the actual shop, including the text fallback for Air Blower. Captures covered both pages and the animated transition. E opened the existing shop and Esc restored input. Stock, wallet, inventory, and saved Hub packages retained their original values. The isolated PIE profile was restored.

The Hub manifest includes the widget's engine materials. The station creates its material instance and render target during loading. The final editor run showed no late widget shader work after Hub readiness. This was a warm editor run. Multiplayer and packaging were outside this check.

On November 12, a standard `MessControlEditor Win64 Development` build replaced the base DLLs. A new editor process loaded them and solo PIE confirmed both rotating shop pages, E and Esc, and the updated slot teaser.
